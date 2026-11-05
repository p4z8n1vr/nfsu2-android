package com.p4z8n1vr.nfsu2

import android.view.Surface

object NativeBridge {

    init {
        System.loadLibrary("nfsu2")
    }

    external fun abiVersion(): Int

    external fun setDataDirectory(
        path: String
    ): Boolean

    external fun attachSurface(
        surface: Surface
    ): Boolean

    external fun detachSurface(): Boolean

    external fun updatePad(
        port: Int,
        connected: Boolean,
        buttons: Int,
        analog: Long,
        lx: Int,
        ly: Int,
        rx: Int,
        ry: Int
    ): Boolean

    external fun getRumble(
        port: Int
    ): Int

    external fun validateGame(
        descriptor: Int,
        filename: String
    ): GameValidationResult

    external fun prepareGame(
        descriptor: Int,
        filename: String
    ): GameSessionResult

    external fun mountGameSession(
        handle: Long
    ): Boolean

    external fun closeGameSession(
        handle: Long
    )

    external fun firstSwapSeen(): Boolean

    external fun resetFirstSwap()
}
