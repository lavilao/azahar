# Zakuro para el Redmi 9A (Android, armeabi-v7a)

Este es [Zakuro](https://github.com/fearkov/zakuro), el emulador de 3DS con
recompilación AOT (en lugar de JIT), portado a Android para el Redmi 9A.

## Por qué esto es rápido donde Azahar era lento

Azahar en armeabi-v7a cae al intérprete porque dynarmic (el JIT) no soporta
ARM de 32 bits como máquina anfitriona. Zakuro usa
[3dsrecomp](https://github.com/fearkov/3dsrecomp): el código ARM del juego se
recompila **por adelantado** en tu PC a C, se compila como biblioteca
**nativa armv7** y el móvil la ejecuta como código nativo. El ~99,8 % de las
instrucciones del juego corren a velocidad nativa; el resto cae al
intérprete integrado.

## Instalación

1. Instala `android/zakuro-redmi9a.apk` (activo "instalar apps de fuentes
   desconocidas"). La app se llama **Zakuro**.
2. Conecta el móvil por USB (MTP) y copia tus ROMs (.3ds/.cxi/.cci/.cia,
   descifradas) a:

   ```
   Android/data/io.github.lavilao.zakuro/files/games
   ```

   La carpeta se crea sola la primera vez que abres la app.
3. Abre Zakuro, toca un juego de la biblioteca y juega.

Controles en pantalla: cruceta táctil a la izquierda (circle pad), A/B/X/Y a
la derecha, L y R en las esquinas de arriba, Start/Select abajo. Tocar la
pantalla inferior del juego = lápiz táctil. El botón **Menu** (arriba a la
derecha) abre el menú (equivale a Esc).

## La parte importante: recompilar tus juegos (PC)

Sin recompilar, el juego corre en el intérprete y en el Redmi 9A será lento.
Recompilado, corre a velocidad nativa. Se hace **una vez por juego**, en un
PC con Linux (o WSL), con tus propias ROMs:

1. Instala Rust (https://rustup.rs) y el NDK de Android (r27 o similar).
2. Instala 3dsrecomp:

   ```
   cargo install --git https://github.com/fearkov/3dsrecomp --locked recomp3ds
   ```

3. Ejecuta el script `android/recompile-game.sh` (en este repo), pasándole
   la ROM y tu NDK:

   ```
   ./android/recompile-game.sh juego.3ds $HOME/Android/Sdk/ndk/27.*
   ```

   Tarda ~10 minutos por juego. Produce `juego.recomp/0004000000XXXXXXX.so`
   (código nativo armv7).
4. Copia ese `.so` al móvil, en:

   ```
   Android/data/io.github.lavilao.zakuro/files/3dsrecomp
   ```

   (crea la carpeta si no existe).
5. Abre el juego en Zakuro: ahora corre a velocidad nativa. El botón
   "Recompile" de la app solo muestra estas instrucciones (no hay compilador
   C en un móvil).

Si actualizas el juego o le pones un mod que cambia el código, recompila
otra vez; lo que cambie cae al intérpreto hasta entonces.

## Guardados, mods y trucos

Todo vive en `Android/data/io.github.lavilao.zakuro/files/zakuro/`:

- `user/` — guardados (formato de zakuro)
- `mods/<title ID>/` — mods con la misma estructura de Luma3DS/Azahar
  (`romfs/`, `exefs/`, ...)
- `settings.toml` — ajustes (volumen, resolución, filtro...)

Los trucos van en `files/zakuro/cheats/<title ID>.txt` con el formato de
Citra/Azahar.

## Detalles técnicos del port

- **ABI**: armeabi-v7a (thumbv7neon: Thumb-2 + NEON, ABI softfp del NDK),
  Android 10 (API 29) como mínimo.
- **Presentación**: Vulkan a través de ash/ash-window
  (VK_KHR_android_surface). El GE8320 expone Vulkan 1.1; el presentador pide
  1.3 y cae a 1.1 (la rasterización 3D por hardware necesita 1.3, así que
  corre en software en el CPU — en este modelo es lo que hay).
- **Ventana**: winit 0.30 con el backend `android-native-activity`
  (NativeActivity, cero Java). El APK no tiene ni un .dex.
- **Audio**: cpal con su backend AAudio de Android.
- **Ciclo de vida**: al ocultar la app se conserva el dispositivo Vulkan (el
  juego sigue donde estaba); al volver se recrea solo la superficie.
- **Entrada**: eventos táctiles crudos de winit (multi-toque real para el
  pad), egui para los menús.

## Compilar el APK tú mismo

Con Rust (1.95+), NDK r27 y SDK (build-tools 34 + platform 34):

```
rustup target add thumbv7neon-linux-androideabi
ANDROID_NDK=/ruta/al/ndk ANDROID_SDK=/ruta/al/sdk ./android/build_apk.sh
```

O deja que GitHub Actions lo construya: el workflow `.github/workflows/
android.yml` del repo produce el APK como artefacto.
# trigger
