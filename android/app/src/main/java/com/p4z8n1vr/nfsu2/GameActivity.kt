package com.p4z8n1vr.nfsu2

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.Process
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.TextView
import java.io.File
import java.util.concurrent.Executors

class GameActivity : Activity(), SurfaceHolder.Callback {

    companion object {
        private const val LOG_TAG = "NFSU2Game"
    }

    private lateinit var gameSurface: SurfaceView
    private lateinit var bootStatus: TextView
    private var touchController: TouchController? = null

    private val executor = Executors.newSingleThreadExecutor()
    private val handler = Handler(Looper.getMainLooper())

    private var bootSubmitted = false

    private val firstFrameCheck = object : Runnable {
        override fun run() {
            val firstFrame = runCatching {
                NativeBridge.firstSwapSeen()
            }.getOrDefault(false)

            if (firstFrame) {
                bootStatus.visibility = View.GONE
                Log.i(LOG_TAG, "First Xbox frame observed")
            } else {
                handler.postDelayed(this, 500L)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestedOrientation =
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

        setContentView(R.layout.activity_game)

        gameSurface = findViewById(R.id.game_boot_surface)
        bootStatus = findViewById(R.id.game_boot_status)

        val filename = intent.getStringExtra(Intent.EXTRA_TITLE)
        val filePath = intent.getStringExtra(Intent.EXTRA_TEXT)
        val uri = intent.data

        if (filename.isNullOrBlank() ||
            !GameFilePicker.isSupportedFile(this, filename) ||
            (filePath == null && uri == null)) {
            bootStatus.setText(R.string.game_boot_invalid_input)
            return
        }

        val initialized = runCatching {
            NativeBridge.abiVersion() == 1 &&
                NativeBridge.setDataDirectory(
                    filesDir.absolutePath
                )
        }.onFailure {
            Log.e(LOG_TAG, "Native initialization failed", it)
        }.getOrDefault(false)

        if (!initialized) {
            bootStatus.setText(R.string.game_boot_failed)
            return
        }

        touchController = TouchController(
            findViewById(R.id.controller_overlay)
        )

        NativeBridge.resetFirstSwap()
        gameSurface.holder.addCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (bootSubmitted) return

        val attached = runCatching {
            NativeBridge.attachSurface(holder.surface)
        }.onFailure {
            Log.e(LOG_TAG, "Vulkan surface attachment failed", it)
        }.getOrDefault(false)

        if (!attached) {
            bootStatus.setText(R.string.game_boot_surface_failed)
            return
        }

        bootSubmitted = true
        bootStatus.setText(R.string.game_boot_starting)

        val filename = intent.getStringExtra(Intent.EXTRA_TITLE)
            ?: return

        val filePath = intent.getStringExtra(Intent.EXTRA_TEXT)
        val uri = intent.data

        executor.execute {
            val result = runCatching {
                bootFromIso(uri, filePath, filename)
            }

            runOnUiThread {
                if (result.getOrDefault(false)) {
                    bootStatus.setText(R.string.game_boot_running)
                    handler.post(firstFrameCheck)
                } else {
                    Log.e(
                        LOG_TAG,
                        "Experimental Xbox boot failed",
                        result.exceptionOrNull()
                    )

                    bootStatus.setText(R.string.game_boot_failed)
                }
            }
        }
    }

    private fun bootFromIso(
        uri: Uri?,
        filePath: String?,
        filename: String
    ): Boolean {
        val descriptor = if (filePath != null) {
            ParcelFileDescriptor.open(
                File(filePath),
                ParcelFileDescriptor.MODE_READ_ONLY
            )
        } else {
            val selectedUri = uri ?: return false

            contentResolver.openFileDescriptor(
                selectedUri,
                "r"
            ) ?: return false
        }

        var sessionHandle = 0L

        try {
            val prepared = descriptor.use {
                NativeBridge.prepareGame(it.fd, filename)
            }

            if (prepared.status != NativeValidationStatus.READY ||
                prepared.handle == 0L) {
                Log.e(
                    LOG_TAG,
                    "ISO validation failed: ${prepared.diagnostic}"
                )
                return false
            }

            sessionHandle = prepared.handle

            if (!NativeBridge.mountGameSession(sessionHandle)) {
                Log.e(LOG_TAG, "Xbox D: mount failed")
                return false
            }

            if (!NativeBridge.bootGameSession(sessionHandle)) {
                Log.e(LOG_TAG, "Xbox kernel boot failed")
                return false
            }

            // A running guest owns the disc until process termination.
            sessionHandle = 0L
            return true
        } finally {
            if (sessionHandle != 0L) {
                NativeBridge.closeGameSession(sessionHandle)
            }
        }
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int
    ) {
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        // No safe renderer teardown while guest threads are active.
        Process.killProcess(Process.myPid())
    }

    override fun onPause() {
        touchController?.disconnect()
        super.onPause()

        // Kill only the experimental :game process, not the launcher.
        Process.killProcess(Process.myPid())
    }
}
