#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace android_game {

enum class ValidationStatus : int32_t {
    Ready = 0,
    UnsupportedFormat = 1,
    InvalidDescriptor = 2,
    NonSeekableDescriptor = 3,
    InvalidDisc = 4,
    MissingExecutable = 5,
    InvalidExecutable = 6,
    WrongTitle = 7,
    WrongEntryPoint = 8,
    WrongRelease = 9,
    MissingGameData = 10,
    GameDirectoryUnavailable = 11,
    ExecutableTooLarge = 12,
    ReadError = 13
};

enum class GameType : int32_t {
    Unknown = 0,
    Iso = 1,
    Xbe = 2
};

struct ValidationResult {
    ValidationStatus status = ValidationStatus::InvalidDescriptor;
    GameType type = GameType::Unknown;

    uint32_t title_id = 0;
    uint32_t entry_point = 0;

    std::string sha1;
    std::string diagnostic;

    [[nodiscard]] bool Ready() const {
        return status == ValidationStatus::Ready;
    }
};

// The caller owns fd; filesystem_path must identify the same file.
//
[[nodiscard]] ValidationResult ValidateFile(
    int fd,
    std::string_view filename,
    std::string_view filesystem_path = {}
);

} // namespace android_game
