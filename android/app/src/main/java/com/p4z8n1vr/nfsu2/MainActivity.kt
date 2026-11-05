package com.p4z8n1vr.nfsu2

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.provider.Settings
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.Button
import android.widget.TextView
import java.io.File
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
    private lateinit var graphicsButton: Button
    private lateinit var selectedGameView: TextView
    private lateinit var selectedGameTypeView: TextView
    private lateinit var selectedGameStatusView: TextView
    private lateinit var validationDetailsView: TextView

    private val validationExecutor = Executors.newSingleThreadExecutor()

    private var validationTask: Future<*>? = null
    private var selectionVersion = 0L

    private var nativeReady = false
    private var surfaceAttached = false
    private var graphicsRequested = false
    private var gameReady = false

    private var selectedGameUri: Uri? = null
    private var selectedGameFilename: String? = null
    private var preparedSessionHandle = 0L
    private var preparationVersion = 0L
    private var preparationTask: Future<*>? = null
    private var pickerOpen = false
    private var manualSelectionActive = false

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
        graphicsButton = findViewById(R.id.graphics_control)
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

        graphicsButton.setOnClickListener {
            if (graphicsRequested) {
                releaseGraphics()
            } else {
                initializeGraphics()
            }
        }

        updateStorageStatus()
        initializeNative()

        surfaceView.holder.addCallback(this)
    }

    override fun onResume() {
        super.onResume()
        updateStorageStatus()

        if (nativeReady &&
            Environment.isExternalStorageManager() &&
            !pickerOpen &&
            !manualSelectionActive
        ) {
            refreshGameLibrary()
        }
    }

    override fun onPause() {
        releaseGraphics()
        super.onPause()
    }

    private fun updateStorageStatus() {
        val granted = Environment.isExternalStorageManager()

        if (!granted && graphicsRequested) {
            releaseGraphics()
        }

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

        updateGraphicsControls()
    }

    private fun updateGraphicsControls() {
        graphicsButton.visibility =
            if (
                gameReady &&
                nativeReady &&
                Environment.isExternalStorageManager()
            ) {
                View.VISIBLE
            } else {
                View.GONE
            }

        graphicsButton.setText(
            if (graphicsRequested) {
                R.string.release_graphics
            } else {
                R.string.initialize_graphics
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
        pickerOpen = true

        runCatching {
            GameFilePicker.open(this)
        }.onFailure {
            pickerOpen = false
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

        pickerOpen = false

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

        manualSelectionActive = true
        selectedGameUri = uri

        val displayName =
            filename ?: getString(R.string.game_filename_unknown)

        selectedGameFilename = displayName

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
        releaseGraphics()
        gameReady = false
        updateGraphicsControls()

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
                    NativeBridge.validateGame(it.fd, filename)
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

                    gameReady = false
                    updateGraphicsControls()

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
        gameReady = result.status == NativeValidationStatus.READY
        updateGraphicsControls()

        val message = getString(
            validationMessage(result.status)
        )

        selectedGameStatusView.text =
            if (gameReady) {
                message
            } else {
                getString(
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
        releaseGraphics()
        manualSelectionActive = false
        gameReady = false
        updateGraphicsControls()

        selectionVersion++
        validationTask?.cancel(true)

        selectedGameUri = null
        selectedGameFilename = null

        selectedGameView.setText(
            R.string.selected_game_none
        )

        selectedGameTypeView.setText(
            R.string.game_type_none
        )

        validationDetailsView.visibility = View.GONE
    }

    private fun initializeGraphics() {
        if (
            !nativeReady ||
            !gameReady ||
            selectedGameUri == null ||
            !Environment.isExternalStorageManager()
        ) {
            return
        }

        if (graphicsRequested) return

        graphicsRequested = true
        updateGraphicsControls()

        showStatus(getString(R.string.graphics_initializing))
        surfaceView.visibility = View.VISIBLE
    }

    private fun releaseGraphics() {
        closePreparedSession()

        if (!graphicsRequested && !surfaceAttached) return

        if (!detachNativeSurface()) return

        graphicsRequested = false
        surfaceView.visibility = View.GONE

        updateGraphicsControls()
        showStatus(getString(R.string.graphics_surface_released))
    }

    private fun refreshGameLibrary() {
        releaseGraphics()
        gameReady = false
        updateGraphicsControls()

        selectionVersion++
        val requestVersion = selectionVersion

        validationTask?.cancel(true)
        validationDetailsView.visibility = View.GONE
        selectedGameStatusView.setText(
            R.string.game_library_scanning
        )

        validationTask = validationExecutor.submit {
            val outcome = runCatching {
                val directory = GameLibrary.ensureDirectory(this)
                val candidates = GameLibrary.candidates(directory)

                var selected: Pair<File, GameValidationResult>? = null

                for (candidate in candidates) {
                    if (Thread.currentThread().isInterrupted) break

                    val validation = runCatching {
                        ParcelFileDescriptor.open(
                            candidate,
                            ParcelFileDescriptor.MODE_READ_ONLY
                        ).use { descriptor ->
                            NativeBridge.validateGame(descriptor.fd, candidate.name)
                        }
                    }.onFailure { error ->
                        Log.w(
                            LOG_TAG,
                            "Unable to validate ${candidate.name}",
                            error
                        )
                    }.getOrNull()

                    if (validation?.status ==
                        NativeValidationStatus.READY
                    ) {
                        selected = candidate to validation
                        break
                    }
                }

                selected
            }

            runOnUiThread {
                if (isFinishing ||
                    isDestroyed ||
                    requestVersion != selectionVersion ||
                    manualSelectionActive
                ) {
                    return@runOnUiThread
                }

                outcome.onSuccess { selected ->
                    if (selected == null) {
                        selectedGameUri = null
                        selectedGameFilename = null
                        selectedGameView.setText(
                            R.string.selected_game_none
                        )
                        selectedGameTypeView.setText(
                            R.string.game_type_none
                        )
                        selectedGameStatusView.setText(
                            R.string.game_library_not_found
                        )
                    } else {
                        val (file, validation) = selected

                        selectedGameUri = Uri.fromFile(file)
                        selectedGameFilename = file.name
                        selectedGameView.text = getString(
                            R.string.selected_game_name,
                            file.name
                        )
                        selectedGameTypeView.text = getString(
                            R.string.selected_game_type,
                            getString(
                                GameFilePicker.getFileType(file.name)
                            )
                        )

                        showValidationResult(validation)
                    }
                }.onFailure { error ->
                    Log.e(LOG_TAG, "Game library scan failed", error)

                    selectedGameUri = null
                    selectedGameFilename = null
                    selectedGameView.setText(
                        R.string.selected_game_none
                    )
                    selectedGameTypeView.setText(
                        R.string.game_type_none
                    )
                    selectedGameStatusView.setText(
                        R.string.game_library_error
                    )
                }
            }
        }
    }

    private fun prepareSelectedIso() {
        val uri = selectedGameUri ?: return
        val filename = selectedGameFilename ?: return

        if (!gameReady || !surfaceAttached) return

        closePreparedSession()

        val requestVersion = preparationVersion
        showStatus(getString(R.string.game_session_preparing))

        preparationTask = validationExecutor.submit {
            val outcome = runCatching {
                contentResolver.openFileDescriptor(uri, "r")?.use {
                    NativeBridge.prepareGame(
                        it.fd,
                        filename
                    )
                } ?: throw FileNotFoundException()
            }

            runOnUiThread {
                val result = outcome.getOrNull()

                if (
                    isFinishing ||
                    isDestroyed ||
                    requestVersion != preparationVersion ||
                    !graphicsRequested ||
                    !surfaceAttached ||
                    !gameReady
                ) {
                    if (result != null && result.handle != 0L) {
                        NativeBridge.closeGameSession(result.handle)
                    }
                    return@runOnUiThread
                }

                outcome.onSuccess { prepared ->
                    if (
                        prepared.status == NativeValidationStatus.READY &&
                        prepared.handle != 0L
                    ) {
                        val mounted = runCatching {
                            NativeBridge.mountGameSession(
                                prepared.handle
                            )
                        }.onFailure { error ->
                            Log.e(
                                LOG_TAG,
                                "Xbox disc mounting failed",
                                error
                            )
                        }.getOrDefault(false)

                        if (mounted) {
                            preparedSessionHandle = prepared.handle

                            showStatus(
                                getString(
                                    R.string.game_disc_mounted,
                                    prepared.executableBytes
                                )
                            )
                        } else {
                            NativeBridge.closeGameSession(
                                prepared.handle
                            )

                            showStatus(
                                getString(
                                    R.string.game_disc_mount_failed
                                )
                            )
                        }
                    } else {
                        showStatus(
                            getString(
                                R.string.game_session_failed,
                                prepared.diagnostic
                            )
                        )
                    }
                }.onFailure { error ->
                    Log.e(
                        LOG_TAG,
                        "ISO session preparation failed",
                        error
                    )

                    showStatus(
                        getString(
                            R.string.game_session_failed,
                            error.message
                                ?: error.javaClass.simpleName
                        )
                    )
                }
            }
        }
    }

    private fun closePreparedSession() {
        preparationVersion++
        preparationTask?.cancel(true)
        preparationTask = null

        val handle = preparedSessionHandle
        preparedSessionHandle = 0L

        if (handle != 0L) {
            NativeBridge.closeGameSession(handle)
        }
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

        updateGraphicsControls()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (
            !graphicsRequested ||
            !nativeReady ||
            surfaceAttached
        ) {
            return
        }

        runCatching {
            NativeBridge.attachSurface(holder.surface)
        }.onSuccess { attached ->
            surfaceAttached = attached

            if (attached) {
                showStatus(
                    getString(R.string.graphics_surface_ready)
                )
                prepareSelectedIso()
            } else {
                graphicsRequested = false
                surfaceView.visibility = View.GONE
                updateGraphicsControls()

                showStatus(
                    getString(
                        R.string.surface_initialization_failed
                    )
                )
            }
        }.onFailure { error ->
            graphicsRequested = false
            surfaceView.visibility = View.GONE
            updateGraphicsControls()

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
        closePreparedSession()

        if (detachNativeSurface()) {
            graphicsRequested = false
            updateGraphicsControls()
        }
    }

    private fun detachNativeSurface(): Boolean {
        if (!surfaceAttached) return true

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

        return detached
    }

    override fun onDestroy() {
        selectionVersion++

        validationTask?.cancel(true)
        validationExecutor.shutdownNow()

        releaseGraphics()
        detachNativeSurface()

        super.onDestroy()
    }
}
