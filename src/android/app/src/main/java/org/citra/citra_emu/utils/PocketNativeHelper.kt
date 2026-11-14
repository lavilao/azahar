// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.Context
import android.net.Uri
import org.citra.citra_emu.model.Game
import java.io.File
import java.io.RandomAccessFile

/**
 * Puente con los paquetes de juego de Pocket Native: carpetas construidas en
 * el PC con el kit (scripts/build.sh --32bit) que contienen:
 *
 *   <carpeta>/game.3ds   ROM descifrada (el motor la usa como datos)
 *   <carpeta>/title.so   código del juego recompilado (se instala en el
 *                        almacenamiento privado la primera vez que corre)
 *   <carpeta>/sysdata/   datos del sistema
 *   <carpeta>/data/      partidas, log y ajustes del motor
 *
 * Los paquetes importados se copian a
 * Android/data/org.azahar_emu.azahar/files/pn-games/<id>/ (el usuario también
 * puede copiarlos ahí a mano con un explorador) y aparecen en la lista de
 * juegos de Azahar con la insignia de Pocket Native.
 */
object PocketNativeHelper {
    private const val GAMES_DIR = "pn-games"
    private const val GAME_FILE = "game.3ds"

    /** Carpeta propia donde viven los paquetes importados. */
    fun gamesDir(context: Context): File =
        File(context.getExternalFilesDir(null) ?: context.filesDir, GAMES_DIR)

    /** Paquetes Pocket Native instalados, como juegos de la lista de Azahar. */
    fun installedGames(context: Context): List<Game> {
        val dir = gamesDir(context)
        val out = mutableListOf<Game>()
        val folders = dir.listFiles { f -> f.isDirectory } ?: return out
        for (folder in folders) {
            if (!File(folder, GAME_FILE).isFile) {
                continue
            }
            out.add(gameOf(folder))
        }
        return out
    }

    /** Construye el Game de Azahar para un paquete ya presente en disco. */
    fun gameOf(folder: File): Game {
        val smdh = Smdh.read(File(folder, GAME_FILE))
        return Game(
            valid = true,
            title = smdh?.title?.takeIf { it.isNotBlank() } ?: folder.name,
            description = "Pocket Native",
            path = folder.absolutePath,
            titleId = 0L,
            mediaType = Game.MediaType.GAME_CARD,
            company = smdh?.publisher?.takeIf { it.isNotBlank() } ?: "",
            regions = smdh?.region ?: "",
            isInstalled = false,
            isSystemTitle = false,
            isVisibleSystemTitle = false,
            isInsertable = false,
            icon = smdh?.icon ?: fallbackIcon(),
            fileType = "Pocket Native",
            isCompressed = false,
            filename = folder.name,
            engine = Game.ENGINE_POCKETNATIVE
        )
    }

    /** Icono plano para paquetes sin SMDH legible: mismo layout que el JNI de
     *  Azahar (píxeles RGB565 de 48x48 reemprequetados en ints little-endian). */
    private fun fallbackIcon(): IntArray {
        val rgb565 = ((0x5B shr 3) shl 11) or ((0x5B shr 2) shl 5) or (0xD6 shr 3)
        val pair = rgb565 or (rgb565 shl 16)
        return IntArray(48 * 48 / 2) { pair }
    }

    /**
     * Importa paquetes Pocket Native desde la carpeta (SAF) que el usuario
     * eligió: admite tanto la carpeta "Pocket Native" completa que genera el
     * kit (pocket-native/games/<id>/...) como una carpeta de juego suelta.
     * Devuelve cuántos juegos nuevos copió.
     */
    fun importFromTree(context: Context, treeUri: Uri): Int {
        var copied = 0
        val root = androidx.documentfile.provider.DocumentFile.fromTreeUri(context, treeUri)
            ?: return 0
        val target = gamesDir(context).apply { mkdirs() }
        copyPackages(context, root, target, depth = 4) { copied++ }
        return copied
    }

    /** Busca recursivamente carpetas con game.3ds y las copia enteras. */
    private fun copyPackages(
        context: Context,
        dir: androidx.documentfile.provider.DocumentFile,
        target: File,
        depth: Int,
        onCopied: () -> Unit
    ) {
        if (depth <= 0) {
            return
        }
        val children = dir.listFiles()
        val isPackage = children.any { it.isFile && it.name.equals(GAME_FILE, ignoreCase = true) }
        if (isPackage) {
            val name = dir.name ?: "juego-${System.currentTimeMillis()}"
            val dest = File(target, name)
            if (File(dest, GAME_FILE).isFile()) {
                return // ya importado
            }
            dest.mkdirs()
            copyDocumentTree(context, children, dest)
            onCopied()
            return
        }
        for (child in children) {
            if (child.isDirectory) {
                copyPackages(context, child, target, depth - 1, onCopied)
            }
        }
    }

    private fun copyDocumentTree(
        context: Context,
        children: Array<androidx.documentfile.provider.DocumentFile>,
        dest: File
    ) {
        for (child in children) {
            val name = child.name ?: continue
            val to = File(dest, name)
            if (child.isDirectory) {
                to.mkdirs()
                copyDocumentTree(context, child.listFiles(), to)
            } else {
                runCatching {
                    context.contentResolver.openInputStream(child.uri)?.use { input ->
                        to.outputStream().use { output -> input.copyTo(output) }
                    }
                }
            }
        }
    }

    /**
     * Copia los perfiles por título empaquetados en assets/pn/titles (los
     * titles/*/profile.toml de pocket-native, con los arreglos de shaders
     * PowerVR por juego) al directorio que el motor lee (filesDir/titles,
     * POCKET_NATIVE_TITLES del android_main). Idempotente por versión.
     */
    fun ensureEngineData(context: Context) {
        val files = context.filesDir
        val titles = File(files, "titles")
        val marker = File(files, ".pn-titles-version")
        val current = runCatching {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName
        }.getOrNull() ?: "0"
        if (marker.isFile && marker.readText() == current && titles.isDirectory) {
            return
        }
        copyAssetDir(context, "pn/titles", titles)
        runCatching { marker.writeText(current) }
    }

    private fun copyAssetDir(context: Context, assetPath: String, dest: File) {
        val children = runCatching { context.assets.list(assetPath) }.getOrNull() ?: return
        if (children.isEmpty()) {
            // es un archivo, no un directorio
            runCatching {
                dest.parentFile?.mkdirs()
                context.assets.open(assetPath).use { input ->
                    dest.outputStream().use { output -> input.copyTo(output) }
                }
            }
            return
        }
        dest.mkdirs()
        for (child in children) {
            copyAssetDir(context, "$assetPath/$child", File(dest, child))
        }
    }
}

/**
 * Lector del SMDH (título, compañía, región e icono) de un .3ds/.cci
 * descifrado, sin depender de la librería nativa: NCSD -> primera partición
 * NCCH -> ExeFS -> entrada "icon".
 */
private object Smdh {
    private const val SMDH_SIZE = 0x3610

    data class Info(
        val title: String,
        val publisher: String,
        val region: String,
        val icon: IntArray
    )

    fun read(rom: File): Info? = runCatching {
        RandomAccessFile(rom, "r").use { f ->
            if (!magic(f, 0x100, "NCSD")) {
                return null
            }
            val ncch = (f.readIntLE(0x120).toLong() and 0xFFFFFFFFL) * 0x200L
            if (!magic(f, ncch + 0x100, "NCCH")) {
                return null
            }
            val exefs = ncch + (f.readIntLE(ncch + 0x1A0).toLong() and 0xFFFFFFFFL) * 0x200L

            // ExeFS: 10 entradas de 16 bytes (nombre[8], offset u32, tamaño u32)
            var iconOffset = -1
            for (i in 0 until 10) {
                val entry = exefs + i * 16L
                val name = f.readString(entry, 8)
                if (name.startsWith("icon")) {
                    iconOffset = f.readIntLE(entry + 8)
                    break
                }
            }
            if (iconOffset < 0) {
                return null
            }
            val smdh = ByteArray(SMDH_SIZE)
            f.seek(exefs + 0x200 + iconOffset)
            if (f.read(smdh) != SMDH_SIZE || !String(smdh, 0, 4, Charsets.US_ASCII).equals("SMDH")) {
                return null
            }
            parse(smdh)
        }
    }.getOrNull()

    private fun parse(b: ByteArray): Info? {
        // 16 bloques de título de 0x200: descripción corta (0x80 UTF-16LE) y
        // publisher (+0x180). 0 = japonés, 1 = inglés.
        fun slot(i: Int): Pair<String, String> {
            val base = 0x08 + i * 0x200
            return utf16(b, base, 0x80) to utf16(b, base + 0x180, 0x80)
        }
        val english = slot(1)
        val title = english.first.ifBlank { slot(0).first }
        if (title.isBlank()) {
            return null
        }
        val publisher = english.second.ifBlank { slot(0).second }
        // banda de región en 0x2018
        val flags = b[0x2018].toInt() and 0xFF
        val region = when {
            flags and 0x1 != 0 && flags and 0x2 != 0 && flags and 0x4 != 0 -> "All regions"
            flags and 0x1 != 0 -> "Japan"
            flags and 0x2 != 0 -> "North America"
            flags and 0x4 != 0 -> "Europe"
            flags and 0x80 != 0 -> "China"
            flags and 0x40 != 0 -> "Korea"
            flags and 0x20 != 0 -> "Taiwan"
            else -> ""
        }
        // icono grande 48x48 RGB565 en 0x24C0. El modelo Game de Azahar
        // guarda los 2304 píxeles u16 como 1152 ints little-endian (pares de
        // píxeles), igual que el JNI game_info.cpp, y el renderizador
        // (GameIconFetcher) los copia directo a un bitmap RGB_565.
        val icon = IntArray(48 * 48 / 2)
        for (i in icon.indices) {
            val p = 0x24C0 + i * 4
            icon[i] = (b[p].toInt() and 0xFF) or (b[p + 1].toInt() and 0xFF shl 8) or
                (b[p + 2].toInt() and 0xFF shl 16) or (b[p + 3].toInt() and 0xFF shl 24)
        }
        return Info(title, publisher, region, icon)
    }

    private fun utf16(b: ByteArray, off: Int, len: Int): String {
        val chars = StringBuilder()
        var i = off
        while (i < off + len) {
            val c = (b[i].toInt() and 0xFF) or (b[i + 1].toInt() and 0xFF shl 8)
            if (c == 0) {
                break
            }
            chars.append(c.toChar())
            i += 2
        }
        return chars.toString().trim()
    }

    private fun magic(f: RandomAccessFile, at: Long, text: String): Boolean =
        f.readString(at, text.length) == text

    private fun RandomAccessFile.readIntLE(at: Long): Int {
        seek(at)
        val b = ByteArray(4)
        if (read(b) != 4) {
            return -1
        }
        return b[0].toInt() and 0xFF or (b[1].toInt() and 0xFF shl 8) or
            (b[2].toInt() and 0xFF shl 16) or (b[3].toInt() and 0xFF shl 24)
    }

    private fun RandomAccessFile.readString(at: Long, len: Int): String {
        seek(at)
        val b = ByteArray(len)
        if (read(b) != len) {
            return ""
        }
        val end = b.indexOf(0.toByte())
        return String(b, 0, if (end < 0) len else end, Charsets.US_ASCII)
    }
}
