# Azahar + 3dsrecomp: 3DS a velocidad nativa en el Redmi 9A

Este fork une dos proyectos para correr juegos de 3DS a velocidad completa en
un telefono de 32 bits sin JIT:

- **Azahar** (la interfaz): biblioteca de juegos, ajustes, mandos,
  renderizador Vulkan y OpenGL ES, audio, partidas guardadas, cheats.
- **3dsrecomp** de [fearkov](https://github.com/fearkov/3dsrecomp) (el
  motor de CPU): recompila el codigo del juego **ahead-of-time** a C y lo
  compila como libreria nativa `armeabi-v7a`. El emulador la ejecuta
  directamente en la CPU del telefono: sin interprete y sin JIT, a la misma
  velocidad que cualquier app nativa. En los juegos probados el 99,7-99,8% de
  las instrucciones corren recompiladas y el resto por el interprete de
  respaldo (FastInterp).

Un telefono de 64 bits no necesita nada de esto: dynarmic (JIT) hace el
trabajo. El Redmi 9A tiene usuariospace de 32 bits, donde dynarmic no
existe: ahi es donde la recompilacion AOT cambia las reglas.

## Como se usa

### 1. Instalar el APK

Descarga el APK del workflow **"Azahar Redmi 9A (32 bits)"** (se compila solo
con cada push) e instalalo en el telefono.

### 2. Recompilar tu juego

1. Sube tu ROM **descifrada** (dump propio) a la carpeta `rom/` de este
   repositorio (es tu fork privado; solo tu la ves). Si 3dsrecomp dice
   "the dump is encrypted", descifrala primero con GodMode9.
2. Pestaña **Actions** -> **"Recompilar juego para Redmi 9A"** -> Run
   workflow. Tarda unos 10-15 minutos: analiza el codigo del juego, genera
   C y lo compila con el clang de Android para `armeabi-v7a`.
3. Al terminar, descarga el artefacto **juego-recompilado-armv7**: dentro
   esta el `<ID-de-titulo>.so` (el nombre es el title ID del juego, en
   hexadecimal).
4. Copia ese archivo al telefono, en la carpeta `recomp/` de tu directorio
   de Azahar (p. ej. `/storage/emulated/0/azahar/recomp/`).

### 3. Jugar

Abre Azahar y lanza el juego. La app copia sola la libreria a su
almacenamiento privado (el unico sitio desde el que se puede cargar un .so)
y el log registrara:

```
codigo recompilado cargado: <title>.so (N puntos de entrada, ...)
```

A partir de ahi la CPU corre a velocidad nativa. Si la libreria no esta, el
juego simplemente corre por el interprete rapido (como antes, lento).

## Detalles que conviene saber

- **ROM descifrada**: 3dsrecomp solo lee dumps descifrados. Los updates/DLC
  deben estar descifrados tambien.
- **Cambiar de version del juego** (update instalado, otro dump, un mod en
  `load/mods`): recompila otra vez. El emulador compara el codigo en memoria
  con el que se recompilo y las funciones que cambiaron pasan al interprete
  (mas lentas) en lugar de usar la libreria vieja: los mods Luma de
  `romfs/` funcionan igual sin recompilar, pero los de `code.bin`/exefs
  conviene recompilarlos.
- **Juegos con modulos CRO** (Persona Q, Monster Hunter...): el backend
  avisa al codigo recompilado de donde se carga cada modulo, asi que tambien
  corren recompilados.
- **Ajuste**: `use_recomp` en la seccion `[Core]` del config.ini (activado
  por defecto). A `0` el backend se comporta como FastInterp puro.
- **Diagnostico de cierres**: si la app se cierra sola, queda un informe en
  `Android/data/org.azahar_emu.azahar/files/` (`azahar-crash.log` para
  senales nativas, `java-crash-*.txt` para excepciones): mandalo y se arregla
  mas rapido que adivinando.

## Arquitectura (donde esta cada pieza)

| Pieza | Sitio |
|---|---|
| ABI con el codigo generado (Context/Host/tablas) | `src/core/arm/recomp/recomp_abi.h` |
| Carga de la libreria por titulo (dlopen, busqueda, stale) | `src/core/arm/recomp/recomp_manager.*` |
| Backend de CPU `ARM_Recomp` (Run/SVC/respaldo FastInterp) | `src/core/arm/recomp/arm_recomp.*` |
| Paso a paso Thumb para el respaldo | `ARM_FastInterp::StepOne` en `src/core/arm/fastinterp/` |
| Seleccion de backend (ARM32 -> recomp) | `src/core/core.cpp` `System::Init` |
| Apertura de libreria al cargar el juego | `src/core/core.cpp` `System::Load` |
| Modulos CRO -> codigo recompilado | `src/core/hle/service/ldr_ro/ldr_ro.cpp` |
| Copia de la libreria al almacenamiento privado | `RecompLibraryHelper.kt` |
| Workflow del APK | `.github/workflows/redmi9a.yml` |
| Workflow de recompilacion de juegos | `.github/workflows/recomp-game.yml` |

La ABI es la version 4 del proyecto 3dsrecomp (`abi/recomp.h` de su repo);
`scripts/test_recomp_abi.cpp` (en la maquina que genero el fork) verifica
campo a campo que ambas cabeceras describen la misma memoria.

## Por que no el interprete, ni el JIT

- El Redmi 9A (Helio G25, Cortex-A53, usuariospace **32 bits**) no puede
  ejecutar dynarmic: solo existe JIT para hosts de 64 bits.
- El interprete rapido mueve ~30-60 MIPS en un A53: los juegos de 3DS
  necesitan ~268 MIPS efectivos.
- El codigo recompilado AOT corre en la propia CPU del telefono: cientos de
  MIPS sin calentar la cabeza con permisos de ejecucion en tiempo de
  ejecucion (el .so se compila en GitHub Actions, el telefono solo lo carga).
