package com.p4z8n1vr.nfsu2

import android.app.Activity
import android.content.pm.ActivityInfo
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView

class MainActivity : Activity(), SurfaceHolder.Callback {

    private lateinit var surfaceView: SurfaceView
    private lateinit var statusView: TextView

    private var nativeReady = false
    private var surfaceAttached = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestedOrientation =
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

        val root = FrameLayout(this).apply {
            setBackgroundColor(Color.BLACK)
        }

        surfaceView = SurfaceView(this)

        root.addView(
            surfaceView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        statusView = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 14f
            setPadding(24, 20, 24, 20)
            setBackgroundColor(Color.argb(170, 0, 0, 0))
        }

        val statusParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP or Gravity.START
        )

        root.addView(statusView, statusParams)

        setContentView(root)

        initializeNative()

        surfaceView.holder.addCallback(this)
    }

    private fun showStatus(message: String) {
        statusView.text = message
    }

    private fun initializeNative() {
        val result = runCatching {
            val version = NativeBridge.abiVersion()

            check(version == 1) {
                "Unsupported native ABI: $version"
            }

            check(
                NativeBridge.setDataDirectory(
                    filesDir.absolutePath
                )
            ) {
                "Failed to configure native data directory"
            }

            version
        }

        result.onSuccess { version ->
            nativeReady = true

            showStatus(
                "NFS Underground 2 Android\n" +
                "Native ABI: $version\n" +
                "Waiting for Vulkan surface..."
            )
        }

        result.onFailure { error ->
            nativeReady = false

            showStatus(
                "Native initialization failed:\n" +
                (error.message ?: error.javaClass.simpleName)
            )
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (!nativeReady || surfaceAttached) return

        runCatching {
            NativeBridge.attachSurface(holder.surface)
        }.onSuccess { attached ->
            surfaceAttached = attached

            if (attached) {
                showStatus(
                    "NFS Underground 2 Android\n" +
                    "JNI: OK\n" +
                    "Vulkan surface: attached\n" +
                    "Game boot: not started"
                )
            } else {
                showStatus(
                    "Vulkan surface initialization failed"
                )
            }
        }.onFailure { error ->
            showStatus(
                "Surface error:\n" +
                (error.message ?: error.javaClass.simpleName)
            )
        }
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int
    ) {
        // Renderer resize handling will be added
        // before enabling game boot.
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        detachNativeSurface()
    }

    private fun detachNativeSurface() {
        if (!surfaceAttached) return

        val detached = runCatching {
            NativeBridge.detachSurface()
        }.getOrDefault(false)

        if (detached) {
            surfaceAttached = false
        } else {
            showStatus(
                "Native surface is still in use.\n" +
                "Renderer shutdown is required."
            )
        }
    }

    override fun onDestroy() {
        detachNativeSurface()
        super.onDestroy()
    }
}
