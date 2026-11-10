// HLSL to SPIR-V for the Vulkan backend. Two compilers, in this order:
//
// 1. DXC (libdxcompiler.so), loaded with dlopen so a machine without it still runs: the same
//    compiler family the Windows build's HLSL comes from, invoked through IDxcCompiler3 with
//    -spirv. CMake defines XBE_HAVE_DXC and adds /opt/dxc/include when DXC is installed.
// 2. glslang's HLSL front end (libglslang, linked): TShader with the HLSL input mode and the
//    HLSL functionality target and explicit SPIR-V resource remapping.
//
// Both are asked for the one descriptor layout vulkan.cpp binds: cbuffers b0/b1 at bindings
// 0/1, textures t0..t3 at 2..5, samplers s0..s3 at 6..9, descriptor set 0. DXC gets it from
// -fvk-b/t/s-shift; glslang bindings are remapped after SPIR-V generation.
#if !defined(_WIN32)

#include "gpu/spirv.h"

#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <mutex>
#include <system_error>
#include <unistd.h>
#include <unordered_map>

#include "core/log.h"
#include "gpu/shaders.h"  // Hash

#if defined(XBE_HAVE_DXC) && __has_include(<dxc/dxcapi.h>)
#define XBE_SPIRV_DXC 1
#include "dxc/WinAdapter.h"
#include "dxc/dxcapi.h"
#endif

#if __has_include(<glslang/Public/ShaderLang.h>)
#define XBE_SPIRV_GLSLANG 1
#ifndef ENABLE_HLSL
#define ENABLE_HLSL 1  // the distro libglslang is built with its HLSL front end; the headers gate its API on this
#endif
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#if __has_include(<glslang/SPIRV/GlslangToSpv.h>)
#include <glslang/SPIRV/GlslangToSpv.h>
#else
#include <SPIRV/GlslangToSpv.h>
#endif
#endif

#if defined(__ANDROID__) && !defined(XBE_SPIRV_GLSLANG)
#error "NFSU2 Android requires glslang for HLSL to SPIR-V compilation"
#endif

namespace gpu {
namespace {

std::mutex g_lock;
std::unordered_map<uint64_t, std::vector<uint32_t>> g_cache;
std::filesystem::path g_dir;
std::string g_compiler = "none";

uint64_t CacheKey(const std::string& hlsl, const char* entry, bool fragment) {
    uint64_t key = Hash(entry, std::strlen(entry), fragment ? 0x7073ull : 0x7673ull);
#if defined(__ANDROID__)
    constexpr char abi[] = "nfsu2-spv-resource-map-v3";
    key = Hash(abi, sizeof(abi) - 1, key);
#endif
    return Hash(hlsl.data(), hlsl.size(), key);
}

std::filesystem::path CacheFile(uint64_t key, bool fragment) {
    char name[48];
    std::snprintf(name, sizeof(name), "%016llx.%s.spv", (unsigned long long)key, fragment ? "ps" : "vs");
    return g_dir / name;
}

// D4: a cache blob is trusted only if it is a whole SPIR-V module, not merely a file
// whose first word is the magic (the magic is written first, so every truncated
// write used to pass). Checks: at least the 5-word header, a sane length, version
// 1.x, a non-zero sane id bound, reserved schema word 0, and an instruction walk
// that lands exactly on the end (a truncation behind the header ends mid-word-count).
bool ValidSpirv(const std::vector<uint32_t>& words) {
    constexpr size_t kMaxWords = 4u * 1024u * 1024u;  // 16 MiB: far above any shader this port generates
    if (words.size() < 5 || words.size() > kMaxWords) return false;
    if (words[0] != 0x07230203u) return false;  // magic
    uint32_t version = words[1];
    if (((version >> 16) & 0xffu) != 1u || ((version >> 8) & 0xffu) > 6u) return false;
    uint32_t bound = words[3];
    if (bound == 0 || bound > 0x400000u) return false;
    if (words[4] != 0) return false;  // schema is reserved and must be 0
    size_t at = 5;
    while (at < words.size()) {
        uint32_t count = words[at] >> 16;
        if (count == 0 || at + count > words.size()) return false;
        at += count;
    }
    return at == words.size();
}

bool ReadWords(const std::filesystem::path& path, std::vector<uint32_t>& words) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize bytes = f.tellg();
    if (bytes < 20 || bytes % 4 || bytes > 16 * 1024 * 1024) return false;  // D4: header + sane length, before allocating
    words.resize(size_t(bytes) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(words.data()), bytes);
    return f.good() && ValidSpirv(words);  // D4
}

void WriteWords(const std::filesystem::path& path, const std::vector<uint32_t>& words) {
    // D4: write to a per-process temporary in the same directory, check the write,
    // then rename over the target: a crash/kill/disk-full mid-write leaves the old
    // entry (or nothing), never a truncated file at the cache name.
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp." + std::to_string(static_cast<long long>(getpid()));
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(reinterpret_cast<const char*>(words.data()), std::streamsize(words.size() * 4));
        f.flush();
        if (!f.good()) { std::filesystem::remove(tmp, ec); return; }
        f.close();
        if (f.fail()) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

#ifdef XBE_SPIRV_DXC
using DxcCreateInstanceFn = HRESULT (*)(REFCLSID rclsid, REFIID riid, LPVOID* ppv);

// libdxcompiler's handle and entry point, taken once; null when the library is not there
DxcCreateInstanceFn DxcEntry() {
    static DxcCreateInstanceFn fn = [] {
        void* lib = dlopen("libdxcompiler.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("/opt/dxc/lib/libdxcompiler.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("/usr/local/lib/libdxcompiler.so", RTLD_NOW | RTLD_LOCAL);
        return lib ? reinterpret_cast<DxcCreateInstanceFn>(dlsym(lib, "DxcCreateInstance")) : nullptr;
    }();
    return fn;
}

std::wstring Wide(const char* s) {
    std::wstring w;
    while (*s) w += wchar_t(uint8_t(*s++));
    return w;
}

bool CompileDxc(const std::string& hlsl, const char* entry, bool fragment, std::vector<uint32_t>& words,
                std::string* error) {
    DxcCreateInstanceFn create = DxcEntry();
    if (!create) return false;
    IDxcUtils* utils = nullptr;
    IDxcCompiler3* compiler = nullptr;
    if (FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
        FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        if (utils) utils->Release();
        if (compiler) compiler->Release();
        if (error) *error = "dxc: DxcCreateInstance failed";
        return false;
    }
    std::wstring wentry = Wide(entry);
    std::wstring wprofile = fragment ? L"ps_6_0" : L"vs_6_0";
    // the binding shifts of the header comment: b in place, t past the two cbuffers, s past them and the textures
    const wchar_t* args[] = {L"-E", wentry.c_str(), L"-T", wprofile.c_str(), L"-spirv",
                             L"-fspv-target-env=vulkan1.2", L"-HV", L"2018", L"-O1",
                             L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", L"2", L"0",
                             L"-fvk-s-shift", L"6", L"0", L"-fvk-u-shift", L"10", L"0"};
    DxcBuffer source{hlsl.data(), hlsl.size(), DXC_CP_UTF8};
    IDxcResult* result = nullptr;
    HRESULT hr = compiler->Compile(&source, args, uint32_t(std::size(args)), nullptr, IID_PPV_ARGS(&result));
    bool ok = false;
    if (SUCCEEDED(hr) && result) {
        HRESULT status = E_FAIL;
        result->GetStatus(&status);
        IDxcBlobUtf8* messages = nullptr;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&messages), nullptr);
        std::string text = messages && messages->GetStringLength() ? messages->GetStringPointer() : "";
        if (messages) messages->Release();
        if (SUCCEEDED(status)) {
            IDxcBlob* blob = nullptr;
            if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&blob), nullptr)) && blob &&
                blob->GetBufferSize() % 4 == 0) {
                words.assign(static_cast<const uint32_t*>(blob->GetBufferPointer()),
                             static_cast<const uint32_t*>(blob->GetBufferPointer()) + blob->GetBufferSize() / 4);
                ok = ValidSpirv(words);  // D4: magic alone is not enough
            }
            if (blob) blob->Release();
        }
        if (!ok && error) *error = "dxc: " + text;
    } else if (error) {
        *error = "dxc: Compile call failed";
    }
    if (result) result->Release();
    compiler->Release();
    utils->Release();
    if (ok) g_compiler = "dxc";
    return ok;
}
#endif  // XBE_SPIRV_DXC

#ifdef XBE_SPIRV_GLSLANG
// glslang may emit separate HLSL textures and samplers at
// overlapping bindings. Vulkan expects textures at 2..5,
// samplers at 6..9, and uniform buffers at 0..1.
bool RemapGlslangResources(
    std::vector<uint32_t>& words,
    std::string* error
) {
    std::unordered_map<uint32_t, uint32_t>
        types, pointees, variables;

    for (size_t i = 5; i < words.size();) {
        uint32_t count = words[i] >> 16;
        uint32_t op = words[i] & 0xFFFF;

        if (!count || count > words.size() - i) {
            if (error)
                *error = "glslang: malformed SPIR-V instructions";
            return false;
        }

        // OpTypeImage, OpTypeSampler, OpTypeSampledImage.
        if ((op == 25 || op == 26 || op == 27) && count >= 2)
            types[words[i + 1]] = op;

        // OpTypePointer: result ID -> pointee type ID.
        if (op == 32 && count >= 4)
            pointees[words[i + 1]] = words[i + 3];

        // OpVariable with UniformConstant storage class.
        if (op == 59 && count >= 4 && words[i + 3] == 0)
            variables[words[i + 2]] = words[i + 1];

        i += count;
    }

    uint32_t images = 0;
    uint32_t samplers = 0;

    for (size_t i = 5; i < words.size();) {
        uint32_t count = words[i] >> 16;
        uint32_t op = words[i] & 0xFFFF;

        // OpDecorate with DecorationBinding (33).
        if (op == 71 && count >= 4 && words[i + 2] == 33) {
            auto variable = variables.find(words[i + 1]);

            if (variable != variables.end()) {
                auto pointer = pointees.find(variable->second);

                if (pointer != pointees.end()) {
                    auto type = types.find(pointer->second);

                    if (type != types.end()) {
                        if (type->second == 27) {
                            if (error)
                                *error =
                                    "glslang: combined image resource "
                                    "unsupported by separate descriptors";
                            return false;
                        }

                        uint32_t slot = words[i + 3];

                        if (slot >= 4) {
                            if (error)
                                *error =
                                    "glslang: unexpected image/sampler "
                                    "register >= 4";
                            return false;
                        }

                        if (type->second == 25) {
                            words[i + 3] = slot + 2;
                            ++images;
                        } else {
                            words[i + 3] = slot + 6;
                            ++samplers;
                        }
                    }
                }
            }
        }

        i += count;
    }

    if (images || samplers) {
        LOGI(
            "spirv: mapped %u images and %u samplers "
            "to Vulkan bindings",
            images, samplers
        );
    }

    return true;
}

bool CompileGlslang(const std::string& hlsl, const char* entry, bool fragment, std::vector<uint32_t>& words,
                    std::string* error) {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
    EShLanguage stage = fragment ? EShLangFragment : EShLangVertex;
    glslang::TShader shader(stage);
    const char* text = hlsl.c_str();
    shader.setStrings(&text, 1);
    shader.setEntryPoint(entry);
    shader.setSourceEntryPoint(entry);
    shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 120);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    shader.setEnvTargetHlslFunctionality1();
    shader.setHlslIoMapping(true);
    // Remapping is applied explicitly to the generated SPIR-V.
    shader.setShiftBinding(glslang::EResUbo, 0);
    shader.setShiftBinding(glslang::EResTexture, 0);
    shader.setShiftBinding(glslang::EResSampler, 0);
    shader.setShiftBinding(glslang::EResCombinedSampler, 0);
    shader.setShiftBinding(glslang::EResImage, 10);
    shader.setAutoMapBindings(false);
    shader.setAutoMapLocations(true);
    if (!shader.parse(GetDefaultResources(), 120, false, EShMsgDefault)) {
        if (error) *error = std::string("glslang: ") + shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) {
        if (error) *error = std::string("glslang: ") + program.getInfoLog();
        return false;
    }
    glslang::SpvOptions options;
    options.generateDebugInfo = false;
    options.optimizeSize = false;
    options.disassemble = false;
    options.validate = true;
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, nullptr, &options);
    if (!ValidSpirv(words)) {  // D4
        if (error) *error = "glslang: no valid SPIR-V produced";
        return false;
    }
    if (!RemapGlslangResources(words, error))
        return false;
    g_compiler = "glslang";
    return true;
}
#endif  // XBE_SPIRV_GLSLANG

}  // namespace

void SetSpirvCacheDir(std::filesystem::path dir, std::string device_key) {
    std::lock_guard lock(g_lock);
    // a folder name, nothing else: keep the characters a path is happy with
    for (char& c : device_key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            c = '_';
    g_dir = dir / device_key;
}

void DiscardCachedSpirv(const std::string& hlsl, const char* entry, bool fragment) {
    // D4: drop both the in-memory words and the on-disk entry, so the next attempt
    // (and the next run) recompiles from HLSL instead of reusing a blob that
    // vkCreateShaderModule rejected.
    uint64_t key = CacheKey(hlsl, entry, fragment);
    std::filesystem::path file;
    {
        std::lock_guard lock(g_lock);
        g_cache.erase(key);
        if (!g_dir.empty()) file = CacheFile(key, fragment);
    }
    if (!file.empty()) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
}

bool CompileHlslToSpirv(const std::string& hlsl, const char* entry, bool fragment, std::vector<uint32_t>& words,
                        std::string* error) {
    uint64_t key = CacheKey(hlsl, entry, fragment);
    {
        std::lock_guard lock(g_lock);
        auto it = g_cache.find(key);
        if (it != g_cache.end()) {
            words = it->second;
            return !words.empty();
        }
    }
    std::filesystem::path file;
    {
        std::lock_guard lock(g_lock);
        if (!g_dir.empty()) file = CacheFile(key, fragment);
    }
    if (!file.empty() && ReadWords(file, words)) {
        std::lock_guard lock(g_lock);
        g_cache[key] = words;
        return true;
    }
    if (!file.empty()) {  // D4: an entry that failed validation is discarded, not left for the next run
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
    std::string dxc_error, glslang_error;
    bool ok = false;
#ifdef XBE_SPIRV_DXC
    ok = CompileDxc(hlsl, entry, fragment, words, &dxc_error);
#endif
#ifdef XBE_SPIRV_GLSLANG
    if (!ok) ok = CompileGlslang(hlsl, entry, fragment, words, &glslang_error);
#endif
    if (!ok) {
        if (error) *error = (dxc_error.empty() ? "" : dxc_error + "\n") + glslang_error;
        // cached as a failure, like d3d12.cpp's Compile: a bad shader is not recompiled every draw
        std::lock_guard lock(g_lock);
        g_cache[key] = {};
        return false;
    }
    {
        std::lock_guard lock(g_lock);
        g_cache[key] = words;
    }
    if (!file.empty()) WriteWords(file, words);
    return true;
}

const char* SpirvCompilerName() {
    std::lock_guard lock(g_lock);
    return g_compiler.c_str();
}

}  // namespace gpu

#endif  // !defined(_WIN32)
