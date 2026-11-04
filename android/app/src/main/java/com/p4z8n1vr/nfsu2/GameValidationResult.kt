package com.p4z8n1vr.nfsu2

data class GameValidationResult(
    val status: Int,
    val type: Int,
    val titleId: Long,
    val entryPoint: Long,
    val sha1: String,
    val diagnostic: String
)

internal object NativeValidationStatus {
    const val READY = 0
    const val UNSUPPORTED_FORMAT = 1
    const val INVALID_DESCRIPTOR = 2
    const val NON_SEEKABLE_DESCRIPTOR = 3
    const val INVALID_DISC = 4
    const val MISSING_EXECUTABLE = 5
    const val INVALID_EXECUTABLE = 6
    const val WRONG_TITLE = 7
    const val WRONG_ENTRY_POINT = 8
    const val WRONG_RELEASE = 9
    const val MISSING_GAME_DATA = 10
    const val GAME_DIRECTORY_UNAVAILABLE = 11
    const val EXECUTABLE_TOO_LARGE = 12
    const val READ_ERROR = 13
}
