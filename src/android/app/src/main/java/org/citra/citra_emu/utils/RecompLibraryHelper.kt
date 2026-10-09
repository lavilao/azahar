// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import java.io.File
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.model.Game

/**
 * Prepara la libreria recompilada AOT (3dsrecomp) del juego que se va a
 * lanzar.
 *
 * El usuario deja la libreria generada para su juego en
 * `<directorio de usuario>/recomp/<title id>.so` (por ejemplo
 * /storage/emulated/0/azahar/recomp/0004000000123400.so). El almacenamiento
 * compartido no permite cargar ejecutables: hay que copiarla al
 * almacenamiento privado de la app, que si es ejecutable (y que solo la
 * propia app puede leer).
 */
object RecompLibraryHelper {
    private const val TAG = "RecompLibraryHelper"

    /** Copia (si hace falta) la libreria del titulo y se la pasa al nucleo. */
    fun prepare(game: Game) {
        val name = "%016X.so".format(game.titleId)
        try {
            val appContext = CitraApplication.appContext
            val dest = File(File(appContext.filesDir, "recomp"), name)
            val source = File(File(NativeLibrary.getUserDirectory(), "recomp"), name)

            if (source.isFile) {
                dest.parentFile?.mkdirs()
                val upToDate = dest.isFile && dest.lastModified() >= source.lastModified() &&
                        dest.length() == source.length()
                if (!upToDate) {
                    source.copyTo(dest, overwrite = true)
                    dest.setReadable(true, false)
                    Log.info("$TAG: copiada ${source.path} -> ${dest.path}")
                }
                Log.info("$TAG: usando libreria recompilada ${dest.path}")
                NativeLibrary.setRecompLibrary(dest.absolutePath)
                return
            }

            // instalada directamente en el almacenamiento privado
            if (dest.isFile) {
                Log.info("$TAG: usando libreria recompilada ${dest.path}")
                NativeLibrary.setRecompLibrary(dest.absolutePath)
                return
            }

            Log.info("$TAG: sin libreria recompilada para ${game.titleId} ($name)")
            NativeLibrary.setRecompLibrary("")
        } catch (e: Exception) {
            Log.error("$TAG: ${e.message}")
            try {
                NativeLibrary.setRecompLibrary("")
            } catch (_: Exception) {
                // el nucleo tratara el vacio como sin libreria
            }
        }
    }
}
