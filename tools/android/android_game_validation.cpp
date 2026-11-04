#include "android_game_validation.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

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

bool HasDiscData(const kernel::Disc& disc) {
    const auto* data_directory =
        disc.Find(std::vector<std::string>{"NFSUNDER"});

    if (!data_directory || !data_directory->dir) {
        return false;
    }

    const auto* index = disc.Find(
        std::vector<std::string>{"NFSUNDER", "ZDIR.BIN"}
    );

    if (!index || index->dir ||
        index->size < 24 ||
        index->size % 24 != 0) {
        return false;
    }

    for (int id = 0; id < 10; ++id) {
        const std::string filename =
            "ZZDATA" + std::to_string(id) + ".BIN";

        const auto* archive = disc.Find(
            std::vector<std::string>{"NFSUNDER", filename}
        );

        if (!archive || archive->dir || archive->size == 0) {
            return false;
        }
    }

    return true;
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

    if (!HasDiscData(disc)) {
        result.status = ValidationStatus::MissingGameData;
        result.diagnostic = "Disc does not contain additional game data";
    }

    return result;
}


} // namespace

ValidationResult ValidateFile(
    int fd,
    std::string_view filename
) {
    if (!EndsWith(Lower(filename), ".iso")) {
        return MakeResult(
            ValidationStatus::UnsupportedFormat,
            GameType::Unknown
        );
    }

    if (fd < 0) {
        return MakeResult(
            ValidationStatus::InvalidDescriptor,
            GameType::Iso
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
            GameType::Iso,
            non_seekable
                ? "Selected descriptor does not support random access"
                : "Unable to read selected descriptor"
        );
    }

    return ValidateIso(fd);
}

} // namespace android_game
