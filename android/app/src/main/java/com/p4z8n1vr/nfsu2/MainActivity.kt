package com.p4z8n1vr.nfsu2

import android.app.Activity
import android.content.ContentResolver
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.Button
import android.widget.TextView
import java.io.FileNotFoundException
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.Future

class MainActivity : Activity(), SurfaceHolder.Callback {

    companion object {
        private const val LOG_TAG = "NFSU2Validation"
    }

    private lateinit var surfaceView: SurfaceView
    private lateinit var statusView: TextView
    private lateinit var storageStatusView: TextView
    private lateinit var storageButton: Button
    private lateinit var selectGameButton: Button
    private lateinit var selectedGameView: TextView
    private lateinit var selectedGameTypeView: TextView
    private lateinit var selectedGameStatusView: TextView
    private lateinit var validationDetailsView: TextView

    private val validationExecutor =
        Executors.newSingleThreadExecutor()

    private var validationTask: Future<*>? = null
    private var selectionVersion = 0L

    private var nativeReady = false
    private var surfaceAttached = false

    private var selectedGameUri: Uri? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestedOrientation =
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY

        setContentView(R.layout.activity_main)

        surfaceView = findViewById(R.id.game_surface)
        statusView = findViewById(R.id.native_status)
        storageStatusView = findViewById(R.id.storage_status)
        storageButton = findViewById(R.id.grant_storage_access)
        selectGameButton = findViewById(R.id.select_game_file)
        selectedGameView = findViewById(R.id.selected_game)
        selectedGameTypeView = findViewById(R.id.selected_game_type)
        selectedGameStatusView = findViewById(R.id.selected_game_status)
        validationDetailsView = findViewById(R.id.game_validation_details)

        storageButton.setOnClickListener {
            requestFullStorageAccess()
        }

        selectGameButton.setOnClickListener {
            openGamePicker()
        }

        updateStorageStatus()
        initializeNative()

        surfaceView.holder.addCallback(this)
    }

    override fun onResume() {
        super.onResume()
        updateStorageStatus()
    }

    private fun updateStorageStatus() {
        val granted = Environment.isExternalStorageManager()

        storageStatusView.setText(
            if (granted) {
                R.string.storage_granted
            } else {
                R.string.storage_not_granted
            }
        )

        storageButton.isEnabled = !granted
        selectGameButton.isEnabled = granted

        storageButton.setText(
            if (granted) {
                R.string.storage_access_granted
            } else {
                R.string.grant_storage_access
            }
        )
    }

    private fun requestFullStorageAccess() {
        if (Environment.isExternalStorageManager()) {
            updateStorageStatus()
            return
        }

        val appSettings = Intent(
            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION
        ).apply {
            data = Uri.parse("package:$packageName")
        }

        val globalSettings = Intent(
            Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION
        )

        val opened = runCatching {
            startActivity(appSettings)
        }.recoverCatching {
            startActivity(globalSettings)
        }.isSuccess

        if (!opened) {
            showStatus(
                getString(R.string.storage_settings_unavailable)
            )
        }
    }

    private fun openGamePicker() {
        runCatching {
            GameFilePicker.open(this)
        }.onFailure {
            selectedGameStatusView.setText(
                R.string.game_picker_unavailable
            )
        }
    }

    @Suppress("DEPRECATION")
    override fun onActivityResult(
        requestCode: Int,
        resultCode: Int,
        data: Intent?
    ) {
        super.onActivityResult(requestCode, resultCode, data)

        if (requestCode != GameFilePicker.REQUEST_CODE) {
            return
        }

        if (resultCode != RESULT_OK) {
            selectedGameStatusView.setText(
                R.string.game_selection_cancelled
            )
            return
        }

        val uri = data?.data

        if (uri == null) {
            clearGameSelection()
            selectedGameStatusView.setText(
                R.string.game_selection_invalid
            )
            return
        }

        handleGameSelection(uri)
    }

    private fun handleGameSelection(uri: Uri) {
        if (!GameFilePicker.isSupportedUri(uri)) {
            clearGameSelection()
            selectedGameStatusView.setText(
                R.string.game_selection_scheme_unsupported
            )
            return
        }

        val filename = GameFilePicker.getDisplayName(this, uri)

        if (
            filename != null &&
            !GameFilePicker.isSupportedFile(this, filename)
        ) {
            clearGameSelection()
            selectedGameStatusView.setText(
                R.string.game_selection_unsupported
            )
            return
        }

        selectedGameUri = uri

        val displayName = filename
            ?: getString(R.string.game_filename_unknown)

        selectedGameView.text = getString(
            R.string.selected_game_name,
            displayName
        )

        selectedGameTypeView.text = getString(
            R.string.selected_game_type,
            getString(GameFilePicker.getFileType(filename))
        )

        startGameValidation(uri, displayName)
    }

    private fun startGameValidation(
        uri: Uri,
        filename: String
    ) {
        selectionVersion++
        val requestVersion = selectionVersion

        validationTask?.cancel(true)
        validationDetailsView.visibility = View.GONE

        selectedGameStatusView.setText(
            R.string.game_validation_running
        )

        validationTask = validationExecutor.submit {
            val outcome = runCatching {
                contentResolver.openFileDescriptor(uri, "r")?.use {
                    NativeBridge.validateGame(
                        it.fd,
                        filename,
                        if (uri.scheme == ContentResolver.SCHEME_FILE) {
                            uri.path
                        } else {
                            null
                        }
                    )
                } ?: throw FileNotFoundException()
            }

            runOnUiThread {
                if (
                    isFinishing ||
                    isDestroyed ||
                    requestVersion != selectionVersion
                ) {
                    return@runOnUiThread
                }

                outcome.onSuccess { result ->
                    showValidationResult(result)
                }.onFailure { error ->
                    Log.e(
                        LOG_TAG,
                        "Game validation failed",
                        error
                    )

                    selectedGameStatusView.setText(
                        R.string.game_validation_io_error
                    )
                }
            }
        }
    }

    private fun validationMessage(status: Int): Int {
        return when (status) {
            NativeValidationStatus.READY ->
                R.string.game_validation_success

            NativeValidationStatus.UNSUPPORTED_FORMAT ->
                R.string.game_selection_unsupported

            NativeValidationStatus.INVALID_DESCRIPTOR ->
                R.string.game_validation_invalid_descriptor

            NativeValidationStatus.NON_SEEKABLE_DESCRIPTOR ->
                R.string.game_validation_non_seekable

            NativeValidationStatus.INVALID_DISC ->
                R.string.game_validation_invalid_disc

            NativeValidationStatus.MISSING_EXECUTABLE ->
                R.string.game_validation_missing_xbe

            NativeValidationStatus.INVALID_EXECUTABLE ->
                R.string.game_validation_invalid_xbe

            NativeValidationStatus.WRONG_TITLE ->
                R.string.game_validation_wrong_title

            NativeValidationStatus.WRONG_ENTRY_POINT ->
                R.string.game_validation_wrong_entry

            NativeValidationStatus.WRONG_RELEASE ->
                R.string.game_validation_wrong_release

            NativeValidationStatus.MISSING_GAME_DATA ->
                R.string.game_validation_missing_data

            NativeValidationStatus.GAME_DIRECTORY_UNAVAILABLE ->
                R.string.game_validation_directory_unavailable

            NativeValidationStatus.EXECUTABLE_TOO_LARGE ->
                R.string.game_validation_xbe_too_large

            NativeValidationStatus.READ_ERROR ->
                R.string.game_validation_read_error

            else ->
                R.string.game_validation_unknown_error
        }
    }

    private fun showValidationResult(
        result: GameValidationResult
    ) {
        val message = getString(
            validationMessage(result.status)
        )

        if (result.status == NativeValidationStatus.READY) {
            selectedGameStatusView.text = message
        } else {
            selectedGameStatusView.text = getString(
                R.string.game_validation_failed,
                message
            )
        }

        if (result.diagnostic.isNotBlank()) {
            Log.i(LOG_TAG, result.diagnostic)
        }

        if (result.sha1.isNotBlank()) {
            val titleId = String.format(
                Locale.ROOT,
                "%08X",
                result.titleId
            )

            val entryPoint = String.format(
                Locale.ROOT,
                "%08X",
                result.entryPoint
            )

            validationDetailsView.text = getString(
                R.string.game_validation_details,
                titleId,
                entryPoint,
                result.sha1
            )

            validationDetailsView.visibility = View.VISIBLE
        } else {
            validationDetailsView.visibility = View.GONE
        }
    }

    private fun clearGameSelection() {
        selectionVersion++

        validationTask?.cancel(true)
        selectedGameUri = null

        selectedGameView.setText(
            R.string.selected_game_none
        )

        selectedGameTypeView.setText(
            R.string.game_type_none
        )

        validationDetailsView.visibility = View.GONE
    }

    private fun showStatus(message: String) {
        statusView.text = message
    }

    private fun initializeNative() {
        val result = runCatching {
            val version = NativeBridge.abiVersion()

            check(version == 1) {
                getString(R.string.unsupported_native_abi, version)
            }

            check(
                NativeBridge.setDataDirectory(filesDir.absolutePath)
            ) {
                getString(R.string.data_directory_failed)
            }

            version
        }

        result.onSuccess { version ->
            nativeReady = true
            showStatus(getString(R.string.native_ready, version))
        }

        result.onFailure { error ->
            nativeReady = false

            showStatus(
                getString(
                    R.string.native_initialization_failed,
                    error.message ?: error.javaClass.simpleName
                )
            )
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (!nativeReady || surfaceAttached) return

        runCatching {
            NativeBridge.attachSurface(holder.surface)
        }.onSuccess { attached ->
            surfaceAttached = attached

            showStatus(
                getString(
                    if (attached) {
                        R.string.surface_attached
                    } else {
                        R.string.surface_initialization_failed
                    }
                )
            )
        }.onFailure { error ->
            showStatus(
                getString(
                    R.string.surface_error,
                    error.message ?: error.javaClass.simpleName
                )
            )
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
                getString(R.string.surface_detach_failed)
            )
        }
    }

    override fun onDestroy() {
        selectionVersion++

        validationTask?.cancel(true)
        validationExecutor.shutdownNow()

        detachNativeSurface()
        super.onDestroy()
    }
}
