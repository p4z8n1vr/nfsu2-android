#include "android_game_validation.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "kernel/crypto.h"
#include "kernel/disc.h"
#include "xbe/xbe.h"
#include "nfsu2_expected_xbe.h"

namespace android_game {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kExpectedTitle = 0x45410071;
constexpr uint32_t kExpectedEntry = 0x0021B32E;

constexpr size_t kMaxXbeSize = 64ULL * 1024 * 1024;
constexpr size_t kReadChunk = 64 * 1024;

ValidationResult MakeResult(
    ValidationStatus status,
    GameType type,
    std::string diagnostic = {}
) {
    ValidationResult result;
    result.status = status;
    result.type = type;
    result.diagnostic = std::move(diagnostic);
    return result;
}

std::string Lower(std::string_view value) {
    std::string result(value);

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    return result;
}

bool EndsWith(
    std::string_view value,
    std::string_view suffix
) {
    return value.size() >= suffix.size() &&
        value.substr(value.size() - suffix.size()) == suffix;
}

bool ReadLimited(
    int fd,
    std::vector<uint8_t>& output,
    bool& too_large
) {
    output.clear();
    too_large = false;

    struct stat st{};

    if (fstat(fd, &st) == 0 &&
        S_ISREG(st.st_mode) &&
        st.st_size > static_cast<off_t>(kMaxXbeSize)) {
        too_large = true;
        return false;
    }

    std::array<uint8_t, kReadChunk> buffer{};
    uint64_t offset = 0;

    for (;;) {
        const size_t remaining =
            kMaxXbeSize + 1 - output.size();

        const size_t request =
            std::min(buffer.size(), remaining);

        const ssize_t count = pread(
            fd,
            buffer.data(),
            request,
            static_cast<off_t>(offset)
        );

        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        if (count == 0) return true;

        output.insert(
            output.end(),
            buffer.begin(),
            buffer.begin() + count
        );

        offset += static_cast<uint64_t>(count);

        if (output.size() > kMaxXbeSize) {
            too_large = true;
            output.clear();
            return false;
        }
    }
}

std::string CalculateSha1(
    const std::vector<uint8_t>& bytes
) {
    alignas(8) std::array<uint8_t, 128> context{};
    std::array<uint8_t, 20> digest{};

    crypto::ShaInit(context.data());
    crypto::ShaUpdate(
        context.data(),
        bytes.data(),
        bytes.size()
    );
    crypto::ShaFinal(context.data(), digest.data());

    constexpr char hex[] = "0123456789abcdef";

    std::string result;
    result.reserve(40);

    for (uint8_t byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 0x0F];
    }

    return result;
}

uint32_t ReadU32(
    const std::vector<uint8_t>& bytes,
    size_t offset
) {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

ValidationResult ValidateXbeBytes(
    const std::vector<uint8_t>& bytes,
    GameType type
) {
    if (bytes.size() < 0x180 ||
        std::memcmp(bytes.data(), "XBEH", 4) != 0) {
        return MakeResult(
            ValidationStatus::InvalidExecutable,
            type,
            "Invalid XBE signature or header"
        );
    }

    const uint32_t base = ReadU32(bytes, 0x104);
    const uint32_t image_size = ReadU32(bytes, 0x10C);
    const uint32_t certificate = ReadU32(bytes, 0x118);
    const uint32_t encrypted_entry = ReadU32(bytes, 0x128);
    const uint32_t encrypted_thunk = ReadU32(bytes, 0x158);

    auto inside_image = [base, image_size](uint32_t address) {
        return address >= base &&
            uint64_t(address) - base < image_size;
    };

    uint32_t entry = 0;

    if (inside_image(encrypted_entry ^ xbe::kRetailEntryKey) &&
        inside_image(encrypted_thunk ^ xbe::kRetailThunkKey)) {
        entry = encrypted_entry ^ xbe::kRetailEntryKey;
    } else if (
        inside_image(encrypted_entry ^ xbe::kDebugEntryKey) &&
        inside_image(encrypted_thunk ^ xbe::kDebugThunkKey)
    ) {
        entry = encrypted_entry ^ xbe::kDebugEntryKey;
    } else {
        return MakeResult(
            ValidationStatus::InvalidExecutable,
            type,
            "Unrecognized XBE entry point encoding"
        );
    }

    if (certificate < base) {
        return MakeResult(
            ValidationStatus::InvalidExecutable,
            type,
            "Invalid XBE certificate address"
        );
    }

    const uint64_t certificate_offset =
        uint64_t(certificate) - base;

    if (certificate_offset > bytes.size() ||
        bytes.size() - certificate_offset < 12) {
        return MakeResult(
            ValidationStatus::InvalidExecutable,
            type,
            "XBE certificate is outside the file"
        );
    }

    const uint32_t title = ReadU32(
        bytes,
        static_cast<size_t>(certificate_offset) + 8
    );

    ValidationResult result;
    result.type = type;
    result.title_id = title;
    result.entry_point = entry;
    result.sha1 = CalculateSha1(bytes);

    if (title != kExpectedTitle) {
        result.status = ValidationStatus::WrongTitle;
        result.diagnostic = "Unexpected Xbox Title ID";
        return result;
    }

    if (entry != kExpectedEntry) {
        result.status = ValidationStatus::WrongEntryPoint;
        result.diagnostic = "XBE entry point does not match recompilation";
        return result;
    }

    if (result.sha1 != NFSU2_EXPECTED_XBE_SHA1) {
        result.status = ValidationStatus::WrongRelease;
        result.diagnostic = "XBE SHA-1 does not match recompilation input";
        return result;
    }

    result.status = ValidationStatus::Ready;
    result.diagnostic = "XBE executable verified";
    return result;
}

bool HasDiscData(const kernel::Disc::Entry& directory) {
    for (const auto& entry : directory.children) {
        if (entry.dir) {
            if (HasDiscData(entry)) return true;
        } else if (Lower(entry.name) != "default.xbe") {
            return true;
        }
    }

    return false;
}

bool HasExtractedData(
    int fd,
    std::string_view filesystem_path
) {
    if (filesystem_path.empty()) return false;

    const std::string path_string(filesystem_path);

    struct stat descriptor_stat{};
    struct stat path_stat{};

    if (fstat(fd, &descriptor_stat) != 0 ||
        stat(path_string.c_str(), &path_stat) != 0) {
        return false;
    }

    // The supplied path must point to the selected descriptor.
    if (descriptor_stat.st_dev != path_stat.st_dev ||
        descriptor_stat.st_ino != path_stat.st_ino) {
        return false;
    }

    const fs::path game_directory =
        fs::path(path_string).parent_path();

    if (game_directory.empty()) return false;

    std::error_code error;

    fs::recursive_directory_iterator it(
        game_directory,
        fs::directory_options::skip_permission_denied,
        error
    );

    const fs::recursive_directory_iterator end;

    size_t visited = 0;

    while (!error && it != end && visited++ < 200000) {
        std::error_code entry_error;
        const auto& entry = *it;

        if (entry.is_regular_file(entry_error)) {
            const std::string name =
                Lower(entry.path().filename().string());

            const std::string extension =
                Lower(entry.path().extension().string());

            if (name != "default.xbe" &&
                extension != ".iso" &&
                extension != ".xbe" &&
                extension != ".log") {
                return true;
            }
        }

        it.increment(error);
    }

    return false;
}

ValidationResult ValidateIso(int fd) {
    kernel::Disc disc;
    std::string error;

    if (!disc.OpenFd(fd, error)) {
        return MakeResult(
            ValidationStatus::InvalidDisc,
            GameType::Iso,
            error
        );
    }

    const auto* executable = disc.Find("default.xbe");

    if (!executable || executable->dir) {
        return MakeResult(
            ValidationStatus::MissingExecutable,
            GameType::Iso,
            "Xbox disc has no default.xbe"
        );
    }

    if (executable->size > kMaxXbeSize) {
        return MakeResult(
            ValidationStatus::ExecutableTooLarge,
            GameType::Iso,
            "XBE exceeds the validation size limit"
        );
    }

    std::vector<uint8_t> bytes;

    if (!disc.Load("default.xbe", bytes)) {
        return MakeResult(
            ValidationStatus::ReadError,
            GameType::Iso,
            "Unable to read XBE from disc"
        );
    }

    ValidationResult result =
        ValidateXbeBytes(bytes, GameType::Iso);

    if (!result.Ready()) return result;

    const auto* root =
        disc.Find(std::vector<std::string>{});

    if (!root || !HasDiscData(*root)) {
        result.status = ValidationStatus::MissingGameData;
        result.diagnostic = "Disc does not contain additional game data";
    }

    return result;
}

ValidationResult ValidateXbe(
    int fd,
    std::string_view filesystem_path
) {
    std::vector<uint8_t> bytes;
    bool too_large = false;

    if (!ReadLimited(fd, bytes, too_large)) {
        return MakeResult(
            too_large
                ? ValidationStatus::ExecutableTooLarge
                : ValidationStatus::ReadError,
            GameType::Xbe,
            too_large
                ? "XBE exceeds the validation size limit"
                : "Unable to read XBE"
        );
    }

    ValidationResult result =
        ValidateXbeBytes(bytes, GameType::Xbe);

    if (!result.Ready()) return result;

    if (filesystem_path.empty()) {
        result.status =
            ValidationStatus::GameDirectoryUnavailable;
        result.diagnostic =
            "XBE verified, but its game directory is unresolved";
        return result;
    }

    if (!HasExtractedData(fd, filesystem_path)) {
        result.status = ValidationStatus::MissingGameData;
        result.diagnostic =
            "No accessible additional game data was found";
    }

    return result;
}

} // namespace

ValidationResult ValidateFile(
    int fd,
    std::string_view filename,
    std::string_view filesystem_path
) {
    const std::string name = Lower(filename);

    GameType type = GameType::Unknown;

    if (EndsWith(name, ".iso")) {
        type = GameType::Iso;
    } else if (EndsWith(name, ".xbe")) {
        type = GameType::Xbe;
    } else {
        return MakeResult(
            ValidationStatus::UnsupportedFormat,
            GameType::Unknown
        );
    }

    if (fd < 0) {
        return MakeResult(
            ValidationStatus::InvalidDescriptor,
            type
        );
    }

    uint8_t probe = 0;
    const ssize_t checked = pread(fd, &probe, 1, 0);

    if (checked < 0) {
        const bool non_seekable =
            errno == ESPIPE || errno == EINVAL;

        return MakeResult(
            non_seekable
                ? ValidationStatus::NonSeekableDescriptor
                : ValidationStatus::ReadError,
            type,
            non_seekable
                ? "Selected descriptor does not support random access"
                : "Unable to read selected descriptor"
        );
    }

    if (type == GameType::Iso)
        return ValidateIso(fd);

    return ValidateXbe(fd, filesystem_path);
}

} // namespace android_game
