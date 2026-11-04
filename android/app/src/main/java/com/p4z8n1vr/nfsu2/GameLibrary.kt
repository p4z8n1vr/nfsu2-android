package com.p4z8n1vr.nfsu2

import android.content.Context
import android.os.Environment
import java.io.File
import java.io.IOException
import java.util.Locale

internal object GameLibrary {

    @Suppress("DEPRECATION")
    fun directory(context: Context): File {
        return File(
            Environment.getExternalStorageDirectory(),
            context.getString(R.string.game_directory_name)
        )
    }

    @Throws(IOException::class)
    fun ensureDirectory(context: Context): File {
        if (!Environment.isExternalStorageManager()) {
            throw IOException("Full storage access is required")
        }

        val directory = directory(context)

        if (!directory.isDirectory) {
            if (!directory.mkdirs() && !directory.isDirectory) {
                throw IOException("Unable to create game directory")
            }
        }

        return directory
    }

    @Throws(IOException::class)
    fun candidates(directory: File): List<File> {
        val entries = directory.listFiles()
            ?: throw IOException("Unable to list game directory")

        val files = ArrayList<File>()

        for (entry in entries) {
            if (entry.isFile) {
                files.add(entry)
            } else if (entry.isDirectory) {
                entry.listFiles()
                    ?.filterTo(files) { it.isFile }
            }
        }

        return files
            .filter {
                it.isFile &&
                    it.canRead() &&
                    it.name.endsWith(".iso", ignoreCase = true)
            }
            .sortedBy {
                it.absolutePath.lowercase(Locale.ROOT)
            }

    }
}
