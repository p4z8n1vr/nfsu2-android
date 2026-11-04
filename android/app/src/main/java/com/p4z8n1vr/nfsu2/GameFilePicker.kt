package com.p4z8n1vr.nfsu2

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import java.util.Locale

internal object GameFilePicker {

    const val REQUEST_CODE = 1001

    fun open(activity: Activity) {
        val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "*/*"
            addCategory(Intent.CATEGORY_OPENABLE)

            putExtra(Intent.EXTRA_LOCAL_ONLY, true)
        }

        @Suppress("DEPRECATION")
        activity.startActivityForResult(intent, REQUEST_CODE)
    }

    fun isSupportedUri(uri: Uri): Boolean {
        return uri.scheme == ContentResolver.SCHEME_CONTENT ||
            uri.scheme == ContentResolver.SCHEME_FILE
    }

    fun getDisplayName(
        context: Context,
        uri: Uri
    ): String? {
        if (uri.scheme == ContentResolver.SCHEME_FILE) {
            return uri.lastPathSegment
                ?.takeIf { it.isNotBlank() }
        }

        return runCatching {
            context.contentResolver.query(
                uri,
                arrayOf(OpenableColumns.DISPLAY_NAME),
                null,
                null,
                null
            )?.use { cursor ->
                val index = cursor.getColumnIndex(
                    OpenableColumns.DISPLAY_NAME
                )

                if (index < 0 || !cursor.moveToFirst()) {
                    null
                } else {
                    cursor.getString(index)
                        ?.takeIf { it.isNotBlank() }
                }
            }
        }.getOrNull()
    }

    fun isSupportedFile(
        context: Context,
        filename: String
    ): Boolean {
        val name = filename.lowercase(Locale.ROOT)

        return context.resources.getStringArray(
            R.array.supported_game_extensions
        ).any { extension ->
            name.endsWith(extension.lowercase(Locale.ROOT))
        }
    }

    fun getFileType(filename: String?): Int {
        if (filename == null) {
            return R.string.game_type_unknown
        }

        val name = filename.lowercase(Locale.ROOT)

        return when {
            name.endsWith(".iso") -> R.string.game_type_iso
            else -> R.string.game_type_unknown
        }
    }
}
