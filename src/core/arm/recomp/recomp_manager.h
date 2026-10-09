// gestion de la libreria recompilada por 3dsrecomp para el titulo en curso.
//
// Un proceso solo emula un titulo a la vez y las CPU (nucleos ARM11) se
// ejecutan secuencialmente desde el hilo de emulacion (el mismo modelo que
// FastInterp, que tampoco bloquea), asi que la cache de busquedas no
// necesita cerrojos.

#pragma once

#include <string>
#include <vector>

#include "common/common_types.h"
#include "core/arm/recomp/recomp_abi.h"

namespace Memory {
class MemorySystem;
}

namespace Core::Recomp {

class LibraryManager {
public:
    LibraryManager();
    ~LibraryManager();

    LibraryManager(const LibraryManager&) = delete;
    LibraryManager& operator=(const LibraryManager&) = delete;

    // abre la libreria de un titulo: ruta absoluta a un .so generado por
    // `3dsrecomp build` con CC apuntando a un compilador para la misma ABI
    // (armeabi-v7a). Devuelve false y deja el estado anterior si falla.
    bool Open(const std::string& path, std::string& error);

    // cierra la libreria (al parar el juego).
    void Close();

    bool Loaded() const {
        return handle_ != nullptr;
    }

    // cuantas entradas y modulos cubre el codigo.
    std::string Describe() const;

    // el codigo que puede ejecutarse desde una direccion (bit 0 = Thumb):
    // en el ejecutable o en un modulo colocado. null si no hay. Con una
    // cache de ranuras indexada por la direccion, como la de zakuro.
    Code Lookup(u32 address) const;

    // marca stale a las funciones cuyo codigo en memoria ya no es el que se
    // recompilo (mods, parches), y devuelve cuantas son. Solo con
    // generaciones >= 3; las anteriores no traen la tabla.
    std::size_t CheckStale(Memory::MemorySystem& memory);

    // avisa al codigo donde se cargo un modulo CRO (base 0 al descargarlo)
    // y comprueba su codigo contra la memoria.
    void PlaceModule(const std::string& name, u32 base, Memory::MemorySystem& memory);

    // libreria por defecto del titulo: <dir usuario>/recomp/<id>.so
    static std::string LibraryPathForTitle(u64 program_id);

    // ruta fijada por la interfaz Java antes de lanzar el juego: la libreria
    // ya copiada al almacenamiento privado de la app (dlopen-able).
    static std::string pending_library_path;

private:
    // busca una entrada por direccion en una tabla ordenada.
    Code FindIn(const Entry* entries, std::size_t count, u32 address,
                const Origins* origins, std::size_t base_index) const;

    void* handle_ = nullptr; // dlopen
    Entry* entries_ = nullptr;
    std::size_t entry_count_ = 0;
    Module* modules_ = nullptr;
    std::size_t module_count_ = 0;
    Origins* origins_table_ = nullptr; // una por unidad, o null
    std::size_t origins_units_ = 0;
    u32 generation_ = 0;
    // modulos colocados: base, tamano, indice.
    std::vector<std::tuple<u32, u32, std::size_t>> placed_;

    // cache de busquedas: direccion -> codigo, ranuras por el golden ratio.
    static constexpr std::size_t FOUND_SLOTS = 65536; // potencia de 2
    static std::size_t SlotOf(u32 address);
    mutable std::vector<std::pair<u32, Code>> found_;
};

} // namespace Core::Recomp
