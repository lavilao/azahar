// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.citra.citra_emu.NativeLibrary

/**
 * Diagnostico de cierres inesperados: instala un capturador de excepciones
 * no tratadas y avisa al nucleo de donde escribir los informes de senales
 * nativas (SIGSEGV y compania), para que un cierre a penas arrancar deje
 * un archivo legible en lugar de solo un tombstone invisible.
 */
object CrashDiagnostics {
    private const val TAG = "CrashDiagnostics"
    const val CRASH_FILE = "azahar-crash.log"

    /** directorio donde dejar los informes, legible con un gestor de archivos. */
    fun reportDir(context: Context): File =
        context.getExternalFilesDir(null) ?: File(context.filesDir, "crash")

    fun install(context: Context) {
        // que el nucleo sepa donde escribir sus senales nativas
        try {
            val dir = reportDir(context)
            dir.mkdirs()
            NativeLibrary.setCrashReportDir(File(dir, CRASH_FILE).absolutePath)
        } catch (e: Exception) {
            Log.error("$TAG: no se pudo preparar el directorio de informes: ${e.message}")
        }

        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val dir = reportDir(context)
                dir.mkdirs()
                val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
                val file = File(dir, "java-crash-$stamp.txt")
                file.writeText(
                    buildString {
                        appendLine("Azahar se cerro de forma inesperada")
                        appendLine("hilo: ${thread.name}")
                        appendLine("fecha: $stamp")
                        appendLine()
                        appendLine(throwable.stackTraceToString())
                        appendLine()
                        appendLine("--- logcat (ultimas lineas) ---")
                        try {
                            val log = java.lang.ProcessBuilder("logcat", "-d", "-t", "300")
                                .redirectErrorStream(true)
                                .start()
                                .inputStream.bufferedReader().readText()
                            append(log)
                        } catch (_: Exception) {
                        }
                    }
                )
                android.util.Log.e(TAG, "informe de cierre: ${file.absolutePath}", throwable)
            } catch (_: Exception) {
            }
            // dejar que el proceso muera de verdad
            android.os.Process.killProcess(android.os.Process.myPid())
            Runtime.getRuntime().exit(10)
        }
    }
}
