# Azahar + Pocket Native (implante del motor, APK sin núcleo Citra)

Este fork mantiene **la UI de Azahar** como interfaz y añade el **motor de
Pocket Native** (zakuro con recompilación estática AOT) para jugar en
teléfonos de 32 bits (Redmi 9A: Helio G25, armeabi-v7a, Android 11), donde el
núcleo Citra de Azahar no tiene JIT (dynarmic es solo de 64 bits) y el
intérprete es demasiado lento.

## Cómo funciona

```
┌─────────────────────────────┐      ┌────────────────────────────────┐
│ proceso principal (Azahar)  │      │ proceso :pocketnative          │
│                             │      │                                │
│  lista de juegos, ajustes,  │ Intent extra "game"                 │
│  atajos, insignia por juego ├─────▶│  PocketNativeActivity          │
│  (dex Java/Kotlin, ~5 MB)   │      │   └─ libpocketnative.so        │
│                             │      │      (zakuro: GLES 3.1,        │
│  SIN libcitra-android.so    │      │       fastmem 1 GiB, recomp)   │
└─────────────────────────────┘      └────────────────────────────────┘
```

- **El APK pocket no lleva el núcleo Citra en C++** (se compila con
  `-PpocketOnly=true`, ver abajo). El núcleo solo aporta la emulación
  clásica — intérprete, JIT de 64 bits, GPU PICA, servicios HLE — y eso es
  exactamente lo que Pocket Native reemplaza: el `title.so` recompilado ya
  ES el juego en código ARM nativo, y el runtime de zakuro le da los
  servicios del sistema, el audio y el render GLES 3.1. Sin core, el APK
  pierde ~27 MB de libcitra-android.so, 16 MB de capas de validación de
  Vulkan y horas de compilación CMake en la CI.
- Los juegos **Pocket Native** (paquetes recompilados en el PC con el kit)
  aparecen en la lista con la insignia "Pocket Native" y arrancan el motor en
  un **proceso propio**. Ahí está la clave del 32 bits: el motor reserva
  1 GiB de fastmem (`zakuro_core::reserve_early`) en un espacio de direcciones
  limpio; sin proceso propio, cualquier otra librería fragmentaría ese
  espacio y el resultado son los SIGBUS (señal 7) del enfoque en-proceso
  anterior.
- Sin núcleo, la biblioteca muestra únicamente paquetes PN: los ROM crudos
  (`.3ds`/`.cia`) no son jugables en el APK pocket y por eso no se listan.
  En un teléfono de 64 bits se puede compilar el APK completo (sin
  `-PpocketOnly`) para tener además la emulación clásica de siempre.
- No hay selección de motor a mano: **cada juego corre su motor por diseño**
  (los PN con recompilación estática; sin JIT ni intérprete a elegir), y la
  insignia indica cuál usa cada entrada de la lista.

## Compilar los juegos en el PC (una vez por juego)

1. En tu PC (Windows/Linux + Docker o Podman), descarga el kit: el ZIP
   "Source code" de la última versión de
   [whovian64/pocket-native](https://github.com/lavilao/pocket-native)
   (o clona `lavilao/pocket-native`) y descomprímelo.
2. Compila el juego **en 32 bits**:

   ```bash
   scripts/build.sh --32bit ruta/a/tu/juego.3ds --keys ruta/a/aes_keys.txt
   ```

   (en Windows: `Build a game.bat` con `-Bit32`, PowerShell
   `scripts\build.ps1 -Bit32`). El resultado queda en `build/Pocket Native/`,
   con una carpeta por título (`pocket-native/games/<id del título>/` que
   contiene `game.3ds`, `title.so`, `sysdata/` y `data/`).

## En el teléfono

1. Instala el APK de este workflow: **Actions → "Azahar + Pocket Native
   (Redmi 9A)" → última run → azahar-pn-armv7**.
2. Copia la carpeta `Pocket Native` que generó el PC al teléfono (p. ej. a
   `Download`).
3. En Azahar: **Ajustes → Importar juegos Pocket Native** y elige esa carpeta.
   Se copian a `Android/data/org.azahar_emu.azahar/files/pn-games/`.
4. Toca el juego en la lista. El primer arranque de cada juego compila los
   programas de GPU: puede haber pantalla negra o tirones unos minutos; es
   normal y no se repite (caché de shaders del motor).

### Controles dentro del juego

- El motor dibuja **sus propios controles táctiles** (editables desde su
  menú).
- **Atrás** abre/cierra el menú in-game del motor (FN): ajustes de
  rendimiento, pantalla, controles, "Most compatible", salir...
- Gamepad por Bluetooth/USB: soportado directamente por el motor.

## Partidas y reconstrucción con hints

- Partidas y log del motor: dentro de la carpeta del juego en
  `pn-games/<id>/data/`.
- Si un juego corrió partes interpretadas (lento puntual), el motor anota
  dónde en `pn-games/<id>/title.hints`: copia ese archivo a la carpeta del
  juego en el kit del PC, reconstruye (`Rebuild all games`) y vuelve a
  importar; esas partes pasarán a correr a velocidad nativa.

## Compilar el APK (CI)

Workflow `.github/workflows/azahar-pn.yml`: compila
`libpocketnative.so` (armv7) desde `lavilao/pocket-native` (ref ajustable),
la deja en `jniLibs/armeabi-v7a/`, empaqueta los perfiles por título
(`titles/*/profile.toml`, incluidos los arreglos de shaders PowerVR de tu
Redmi 9A) como assets y compila el APK con `assembleVanillaRelease
-PpocketOnly=true` — **sin CMake, sin NDK-build del núcleo, sin capas de
validación**: minutos en vez de horas, y un APK de solo unos MB.
Se dispara con push a `master`/`pn-implant`, con tag `v*` (publica en
Releases) o a mano desde la pestaña Actions (con opción de elegir la ref del
motor).

Para un APK **completo** (UI + núcleo Citra + motor PN, útil en teléfonos de
64 bits) compila sin la propiedad: `./gradlew assembleVanillaRelease`.

## Estado y limitaciones

- Solo `armeabi-v7a` por ahora (la `abiFilter` del repo ya está fijada a
  v7a para la MIUI 32 bits del Redmi 9A). Para arm64 habría que añadir la
  lib del motor compilada a `aarch64-linux-android`.
- El APK pocket no puede: ejecutar ROM crudos, instalar firmware o CIAs,
  gestionar drivers de GPU ni comprimir ROMs — todo eso vive en el núcleo
  Citra, y los ajustes de esas secciones se ocultan en el APK pocket.
- El audio, el render GLES 3.1 y la lógica de guardado son del motor
  pocket-native (probado por el autor en un Redmi 9 con Fantasy Life);
  los ajustes de la UI de Azahar no afectan a los juegos PN — usa el menú
  del propio motor.
- Rune Factory 4 es un problema conocido del driver PowerVR (crash en
  `libusc.so` incluso con el filtro de shaders; ver docs/PLAN.md de
  pocket-native).
