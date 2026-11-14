// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.activities

import android.app.NativeActivity
import android.content.Intent
import android.os.Bundle
import org.citra.citra_emu.utils.PocketNativeHelper

/**
 * Anfitrión del motor Pocket Native (zakuro con recompilación estática) en un
 * proceso propio (`:pocketnative`).
 *
 * La UI sigue siendo la de Azahar: esta actividad solo aloja a
 * libpocketnative.so (android.app.NativeActivity). Correr el motor en su
 * proceso deja el espacio de direcciones limpio para su reserva de fastmem de
 * 1 GiB (zakuro_core::reserve_early), lo que en un host de 32 bits es la
 * diferencia entre el recompilado a velocidad nativa y el fallback
 * interpretado (o directamente SIGBUS).
 *
 * Contrato con el motor (android/src/lib.rs de pocket-native):
 *  - extra "game": ruta de la carpeta del juego (game.3ds + title.so + ...)
 *  - el motor llama a finishWith(codigo) al terminar; 3 = Reset (relanzar)
 */
class PocketNativeActivity : NativeActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        // Perfiles por título del motor (arreglos de shaders PowerVR, etc.)
        // antes de que NativeActivity cargue la librería y arranque el juego.
        PocketNativeHelper.ensureEngineData(this)
        super.onCreate(savedInstanceState)
    }

    /** Llamada por JNI desde el motor cuando el juego termina. */
    @SuppressWarnings("unused")
    fun finishWith(code: Int) {
        setResult(code)
        if (code == RESET_CODE) {
            // Reset del propio juego: relanzamos esta misma actividad
            val relaunch = Intent(intent)
            relaunch.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
            startActivity(relaunch)
        }
        finish()
    }

    companion object {
        const val RESET_CODE = 3
        const val EXTRA_GAME = "game"
    }
}
