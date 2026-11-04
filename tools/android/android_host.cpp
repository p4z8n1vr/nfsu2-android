#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <thread>

#include <android/native_window.h>

#include "app/app.h"
#include "app/platform.h"
#include "gpu/vulkan.h"

namespace {

std::mutex g_platform_lock;
std::recursive_mutex g_ui_lock;

ANativeWindow* g_window = nullptr;
std::filesystem::path g_data_dir;

constexpr float kWideAspect = 16.0f / 9.0f;

float SelectedAspect() {
    const auto& setting = app::Get().aspect_ratio;

    if (setting == "4:3") return 4.0f / 3.0f;
    if (setting == "16:9") return kWideAspect;
    if (setting == "16:10") return 16.0f / 10.0f;
    if (setting == "21:9") return 21.0f / 9.0f;
    if (setting == "32:9") return 32.0f / 9.0f;

    auto output = app::Output();
    return output.height
        ? float(output.width) / float(output.height)
        : kWideAspect;
}

} // namespace

namespace app {

const GameInfo kGame{
    .title = L"Need for Speed: Underground 2",
    .short_title = L"NFS Underground 2",
    .name = L"NFSU2Android",
    .log = "nfsu2",
    .settings = "nfsu2.toml",
    .splash = nullptr,
    .missing = L"Original Xbox game files are required.",
    .images = {{"default.xbe", "nfsu2"}},
    .options = {},
    .load = nullptr,
    .save = nullptr,
    .init = nullptr,
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = nullptr,
    .pad = nullptr,
    .rumble = nullptr,
    .picture_top = 0,
    .picture_height = 0
};

Settings& Get() {
    static Settings settings;
    return settings;
}

std::filesystem::path ExeDir() {
    std::lock_guard lock(g_platform_lock);
    return g_data_dir;
}

int Instance() {
    // A single Android Activity owns the current game instance.
    return 0;
}

void* Window() {
    return platform::NativeHandle();
}

std::recursive_mutex& UiLock() {
    return g_ui_lock;
}

Mode Desktop() {
    auto size = platform::DesktopSize();
    return {size.width, size.height};
}

Mode Output() {
    const auto& settings = Get();

    if (settings.width && settings.height) {
        return {settings.width, settings.height};
    }

    return Desktop();
}

bool Widescreen() {
    auto output = Output();
    return output.height &&
           uint64_t(output.width) * 2 >=
           uint64_t(output.height) * 3;
}

float AspectWiden() {
    if (!Widescreen() || !Get().ultrawide) {
        return 1.0f;
    }

    return std::clamp(
        SelectedAspect() / kWideAspect,
        1.0f,
        4.0f
    );
}

float TargetAspect() {
    return Widescreen()
        ? kWideAspect * AspectWiden()
        : 4.0f / 3.0f;
}

float HostAspect() {
    // Keep experimental Hor+ disabled by default.
    static const bool enabled = [] {
        const char* value = std::getenv("NFSU2_HORPLUS");
        return value && *value && std::strcmp(value, "0") != 0;
    }();

    if (!enabled || !Get().ultrawide) return 0.0f;

    float aspect = TargetAspect();

    return aspect > kWideAspect + 0.0001f
        ? aspect
        : 0.0f;
}

float RenderScale(uint32_t guest_height) {
    if (!guest_height) return 1.0f;

    float desired_height =
        float(Output().height) *
        (float(Get().render_percent) / 100.0f);

    return std::max(
        desired_height / float(guest_height),
        0.1f
    );
}

void Pace() {
    using Clock = std::chrono::steady_clock;

    thread_local Clock::time_point next{};

    uint32_t cap = Get().fps_cap;

    if (!cap) {
        next = Clock::now();
        return;
    }

    auto period = std::chrono::nanoseconds(
        1000000000ULL / cap
    );

    auto now = Clock::now();

    if (next == Clock::time_point{} ||
        next + period < now) {
        next = now;
    }

    next += period;
    std::this_thread::sleep_until(next);
}

} // namespace app

namespace app::platform {

NativeWindow NativeHandle() {
    std::lock_guard lock(g_platform_lock);
    return g_window;
}

Size ClientSize() {
    std::lock_guard lock(g_platform_lock);

    if (!g_window) return {};

    int width = ANativeWindow_getWidth(g_window);
    int height = ANativeWindow_getHeight(g_window);

    return {
        width > 0 ? uint32_t(width) : 0,
        height > 0 ? uint32_t(height) : 0
    };
}

Size DesktopSize() {
    auto size = ClientSize();

    if (size.width && size.height) return size;

    return {1280, 720};
}

} // namespace app::platform

extern "C" __attribute__((visibility("default")))
bool nfsu2_android_set_data_dir(const char* directory) {
    if (!directory || !*directory) return false;

    std::filesystem::path path(directory);
    std::error_code error;

    if (!std::filesystem::is_directory(path, error)) {
        return false;
    }

    std::lock_guard lock(g_platform_lock);
    g_data_dir = std::move(path);
    return true;
}

extern "C" __attribute__((visibility("default")))
bool nfsu2_android_attach_window(ANativeWindow* window) {
    if (!window) return false;

    std::lock_guard lock(g_platform_lock);

    // Window replacement requires renderer shutdown first.
    if (g_window) return false;

    const char* extensions[] = {
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };

    gpu::SetVulkanInstanceExtensions(extensions, 1);

    VkInstance instance = gpu::VulkanInstance();
    if (instance == VK_NULL_HANDLE) return false;

    auto create_surface =
        reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
            vkGetInstanceProcAddr(
                instance,
                "vkCreateAndroidSurfaceKHR"
            )
        );

    if (!create_surface) return false;

    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType =
        VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;

    if (create_surface(instance, &info, nullptr, &surface)
        != VK_SUCCESS) {
        return false;
    }

    ANativeWindow_acquire(window);
    g_window = window;

    // The Vulkan backend takes ownership of the surface.
    gpu::SetVulkanSurface(instance, surface);

    return true;
}

extern "C" __attribute__((visibility("default")))
bool nfsu2_android_detach_window() {
    std::lock_guard lock(g_platform_lock);

    if (!g_window) return true;

    // Do not destroy a surface owned by an active renderer.
    // The full renderer shutdown path will be implemented
    // before Android game boot is enabled.
    if (gpu::GetVulkanHandles().device != VK_NULL_HANDLE)
        return false;

    VkInstance instance = gpu::VulkanInstance();
    VkSurfaceKHR surface = gpu::VulkanSurface();

    if (instance == VK_NULL_HANDLE)
        return false;

    if (surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surface, nullptr);

    gpu::SetVulkanSurface(instance, VK_NULL_HANDLE);

    ANativeWindow_release(g_window);
    g_window = nullptr;

    return true;
}
