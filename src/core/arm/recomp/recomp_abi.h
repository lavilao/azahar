// interfaz entre Azahar y el codigo 3dsrecomp recompila a ahead-of-time (AOT).
//
// Este encabezado es el lado anfitrion (host) de la ABI version 4 del
// proyecto 3dsrecomp (github.com/fearkov/3dsrecomp, abi/recomp.h). El codigo
// generado incluye su propia copia de recomp.h; aqui solo se replican las
// estructuras de datos que el anfitrion debe conocer, con exactamente la
// misma disposicion en memoria (C estructuras sin padding oculto, punteros
// del tamano del anfitrion, que en armeabi-v7a son de 32 bits y coinciden
// con los de la libreria generada compilada para la misma ABI).
//
// Como funciona: 3dsrecomp lee el codigo del juego, lo convierte en C y lo
// compila en una libreria compartida con un simbolo por titulo. Azahar hace
// dlopen de esa libreria y ejecuta el codigo recompilado directamente en la
// CPU del telefono, sin JIT y sin interprete: a velocidad nativa. Las
// instrucciones que el recompilador no cubrio (menos del 0,3% en los juegos
// probados) pasan por el interprete de respaldo (FastInterp).
//
// SPDX-License-Identifier: MIT (interfaz recomp de fearkov/3dsrecomp)

#pragma once

#include <cstdint>

#include "common/common_types.h"

namespace Core::Recomp {

// version de la interfaz que el codigo generado debe coincidir.
constexpr u32 ABI_VERSION = 4;
// version del generador de codigo; por debajo de la actual significa que
// recompilar de nuevo da codigo mejor.
constexpr u32 GENERATION = 3;

// por que el codigo devolvio el control al anfitrion.
enum ExitReason : u32 {
    EXIT_NONE = 0,
    // un svc: el numero queda en Context::svc y r15 apunta despues de el.
    EXIT_SVC = 1,
    // el presupuesto se agoto: r15 es donde reanudar.
    EXIT_BUDGET = 2,
    // cualquier otra cosa: el anfitrion sigue desde r15.
    EXIT_UNWIND = 3,
};

using Code = void (*)(struct Context* ctx);

// funciones que el anfitrion provee al codigo recompilado.
struct Host {
    std::uint8_t (*read8)(struct Context*, std::uint32_t);
    std::uint16_t (*read16)(struct Context*, std::uint32_t);
    std::uint32_t (*read32)(struct Context*, std::uint32_t);
    void (*write8)(struct Context*, std::uint32_t, std::uint8_t);
    void (*write16)(struct Context*, std::uint32_t, std::uint16_t);
    void (*write32)(struct Context*, std::uint32_t, std::uint32_t);
    // ejecuta una instruccion que el recompilador dejo al interprete.
    void (*interpret)(struct Context*, std::uint32_t address, std::uint32_t opcode);
    // el codigo para una direccion (bit 0 = Thumb), o null.
    Code (*lookup)(struct Context*, std::uint32_t address);
};

struct Context {
    std::uint32_t r[16];
    std::uint8_t n, z, c, v, q, thumb, ge;
    // si ldrex marco exclusive_address.
    std::uint8_t exclusive;
    std::int32_t budget;
    std::uint32_t exit;
    std::uint32_t svc;
    std::uint32_t depth;
    std::uint32_t exclusive_address;
    // el registro de solo lectura de id de hilo, con la TLS del hilo.
    std::uint32_t tls;
    // un puntero del anfitrion por pagina de 4 KiB, o null para que el
    // anfitrion atienda el acceso (IO, paginas especiales).
    std::uint8_t* const* read_pages;
    std::uint8_t* const* write_pages;
    // los registros VFP, s0..s31 (dN ocupa s2N y s2N+1), y el fpscr.
    std::uint32_t* vfp;
    std::uint32_t* fpscr;
    const Host* host;
    void* user;
};

struct Entry {
    std::uint32_t address;
    Code code;
};

// el codigo de un modulo CRO, con direcciones relativas a donde se cargue.
struct Module {
    const char* name;
    // donde el anfitrion lo cargo: el codigo generado lo lee.
    std::uint32_t* base;
    std::uint32_t size;
    std::uint32_t count;
    const Entry* entries;
};

// un trozo de codigo huesped: de start a end, como direcciones o como
// desplazamientos en un modulo, y su hash FNV-1a de 64 bits.
struct Piece {
    std::uint32_t start;
    std::uint32_t end;
    std::uint64_t hash;
};

// una corrida de piezas de una funcion, como indices.
struct Run {
    std::uint32_t first;
    std::uint32_t end;
};

// el codigo de una funcion: count corridas desde runs[first], y donde empieza.
struct Origin {
    std::uint32_t first;
    std::uint32_t count;
    std::uint32_t start;
};

constexpr std::uint32_t NO_ORIGIN = 0xFFFFFFFFu;
constexpr std::uint32_t NO_START = 0xFFFFFFFFu;

// el codigo del ejecutable o de un modulo: sus funciones y que entrada
// ejecuta cada una. El anfitrion marca stale a una funcion cuyo codigo en
// memoria ya no es el que se recompilo (un mod lo cambio).
struct Origins {
    std::uint32_t count;
    std::uint32_t piece_count;
    const Piece* pieces;
    const Origin* functions;
    const Run* runs;
    // para cada entrada, la funcion que la ejecuta, o NO_ORIGIN.
    const std::uint32_t* owners;
    std::uint8_t* stale;
    // el inicio con el que cada funcion compara r15 al entrar; el anfitrion
    // lo pone en NO_START para una funcion stale.
    std::uint32_t* starts;
};

} // namespace Core::Recomp
