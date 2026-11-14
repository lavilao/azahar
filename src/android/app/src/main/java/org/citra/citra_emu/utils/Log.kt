// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.util.Log as AndroidLog
import org.citra.citra_emu.NativeLibrary

/**
 * Fachada de logging de Azahar.
 *
 * Con el núcleo Citra (APK completo) cada mensaje pasa a la librería nativa,
 * que lo escribe al archivo de log de Azahar además de a logcat. En el APK
 * pocket (-PpocketOnly=true, sin núcleo Citra en C++) no existe
 * libcitra-android.so: el log va directo a logcat de Android y el resto de la
 * app no necesita saberlo.
 */
object Log {
    // Tracks whether we should share the old log or the current log
    var gameLaunched = false

    private const val TAG = "Azahar"

    fun debug(message: String) {
        if (NativeLibrary.isCoreAvailable) nativeDebug(message) else AndroidLog.d(TAG, message)
    }

    fun warning(message: String) {
        if (NativeLibrary.isCoreAvailable) nativeWarning(message) else AndroidLog.w(TAG, message)
    }

    fun info(message: String) {
        if (NativeLibrary.isCoreAvailable) nativeInfo(message) else AndroidLog.i(TAG, message)
    }

    fun error(message: String) {
        if (NativeLibrary.isCoreAvailable) nativeError(message) else AndroidLog.e(TAG, message)
    }

    fun critical(message: String) {
        if (NativeLibrary.isCoreAvailable) nativeCritical(message) else AndroidLog.e(TAG, message)
    }

    // Los símbolos JNI viven en jni/native_log.cpp con estos mismos nombres.
    private external fun nativeDebug(message: String)

    private external fun nativeWarning(message: String)

    private external fun nativeInfo(message: String)

    private external fun nativeError(message: String)

    private external fun nativeCritical(message: String)
}
