package com.p4z8n1vr.nfsu2

data class GameSessionResult(
    val status: Int,
    val handle: Long,
    val executableBytes: Long,
    val diagnostic: String
)
