// SPDX-FileCopyrightText: 2026 Azahar Emulator Project (backend recomp AOT)
// SPDX-License-Identifier: GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/arm/recomp/recomp_manager.h"

#include <dlfcn.h>

#include <cstring>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/memory.h"

namespace Core::Recomp {

// la ruta fijada por la interfaz Java (libreria ya copiada a almacenamiento
// privado de la app). Vacia = no hay.
std::string LibraryManager::pending_library_path;

// hash FNV-1a de 64 bits sobre los bytes de una funcion, igual que el
// recompilador: para detectar codigo cambiado en memoria.
namespace {
struct CodeHash {
    u64 value = 0xCBF2'9CE4'8422'2325ull;
    void Add(const u8* bytes, std::size_t length) {
        for (std::size_t i = 0; i < length; i++) {
            value = (value ^ bytes[i]) * 0x0100'0000'01B3ull;
        }
    }
};
} // namespace

LibraryManager::LibraryManager() = default;

LibraryManager::~LibraryManager() {
    Close();
}

void LibraryManager::Close() {
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    entries_ = nullptr;
    entry_count_ = 0;
    modules_ = nullptr;
    module_count_ = 0;
    origins_table_ = nullptr;
    origins_units_ = 0;
    generation_ = 0;
    placed_.clear();
    found_.clear();
}

bool LibraryManager::Open(const std::string& path, std::string& error) {
    if (!FileUtil::Exists(path)) {
        error = "no existe: " + path;
        return false;
    }

    // las capturas se liberan solo con el exito: con fallo se devuelve al
    // estado anterior, que suele ser "sin libreria".
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        error = std::string("dlopen: ") + dlerror();
        return false;
    }

    auto symbol = [&](const char* name) -> void* {
        void* ptr = dlsym(handle, name);
        if (!ptr) {
            error = std::string("falta el simbolo ") + name;
        }
        return ptr;
    };

    u32 abi = *static_cast<u32*>(symbol("recomp_abi"));
    if (!error.empty()) {
        dlclose(handle);
        return false;
    }
    if (abi != ABI_VERSION) {
        error = "la libreria usa la version " + std::to_string(abi) +
                " de la interfaz y Azahar implementa la " + std::to_string(ABI_VERSION) +
                ": recompila el juego con un 3dsrecomp actualizado";
        dlclose(handle);
        return false;
    }

    Entry* entries = static_cast<Entry*>(symbol("recomp_entries"));
    u32* entry_count = static_cast<u32*>(symbol("recomp_entry_count"));
    Module* modules = static_cast<Module*>(symbol("recomp_modules"));
    u32* module_count = static_cast<u32*>(symbol("recomp_module_count"));
    if (!error.empty()) {
        dlclose(handle);
        return false;
    }

    u32 generation = 0;
    if (void* gen = dlsym(handle, "recomp_generation")) {
        generation = *static_cast<u32*>(gen);
    }
    // la generacion 2 ordenaba la tabla de otra forma: nunca se distribuyo
    Origins* origins_table = nullptr;
    std::size_t origins_units = 0;
    if (generation >= 3) {
        if (void* origins = dlsym(handle, "recomp_origins")) {
            origins_table = static_cast<Origins*>(origins);
            // una tabla para el ejecutable y una por modulo
            origins_units = 1 + *module_count;
        }
    }

    Close();
    handle_ = handle;
    entries_ = entries;
    entry_count_ = *entry_count;
    modules_ = modules;
    module_count_ = *module_count;
    origins_table_ = origins_table;
    origins_units_ = origins_units;
    generation_ = generation;
    placed_.clear();
    found_.assign(FOUND_SLOTS, {0xFFFFFFFFu, nullptr});

    LOG_INFO(Core_ARM11,
             "codigo recompilado cargado: {} ({})", path,
             Describe());
    if (generation_ < GENERATION) {
        LOG_WARNING(Core_ARM11,
                    "la libreria es de la generacion {} y la actual es {}: recompilar "
                    "el juego de nuevo ejecuta mas rapido",
                    generation_, GENERATION);
    }
    return true;
}

std::string LibraryManager::Describe() const {
    if (!Loaded()) {
        return "sin libreria recompilada";
    }
    std::size_t functions = 0;
    if (origins_table_) {
        functions = origins_table_[0].count;
    }
    return fmt::format("{} puntos de entrada, {} funciones, {} modulos", entry_count_,
                       functions, module_count_);
}

std::size_t LibraryManager::SlotOf(u32 address) {
    // direcciones ARM avanzan de 4 en 4 y las Thumb de 2 con el bit 0 a 1:
    // el golden ratio sobre los bits altos reparte ambas.
    constexpr std::size_t MASK = FOUND_SLOTS - 1;
    return (address * 0x9E37'79B1u) >> (32 - 16) & MASK;
}

Code LibraryManager::Lookup(u32 address) const {
    if (!Loaded()) {
        return nullptr;
    }
    auto& slot = found_[SlotOf(address)];
    if (slot.first == address) {
        return slot.second;
    }
    Code code = FindIn(entries_, entry_count_, address, origins_table_ ? &origins_table_[0] : nullptr, 0);
    if (!code) {
        for (auto& [base, size, index] : placed_) {
            if (address - base < size) {
                const Module& module = modules_[index];
                code = FindIn(module.entries, module.count, address - base,
                              origins_table_ ? &origins_table_[index + 1] : nullptr,
                              index + 1);
                break;
            }
        }
    }
    slot = {address, code};
    return code;
}

Code LibraryManager::FindIn(const Entry* entries, std::size_t count, u32 address,
                            const Origins* origins, std::size_t base_index) const {
    // busqueda binaria por direccion exacta
    std::size_t low = 0, high = count;
    while (low < high) {
        std::size_t mid = (low + high) / 2;
        if (entries[mid].address < address) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low >= count || entries[low].address != address) {
        return nullptr;
    }
    // una funcion stale no se ejecuta: el interprete se hace cargo
    if (origins && origins->owners) {
        u32 owner = origins->owners[low];
        if (owner != NO_ORIGIN && origins->stale[owner] != 0) {
            return nullptr;
        }
    }
    return entries[low].code;
}

std::size_t LibraryManager::CheckStale(Memory::MemorySystem& memory) {
    if (!origins_table_) {
        return 0;
    }
    std::size_t stale_total = 0;
    for (std::size_t unit = 0; unit < origins_units_; unit++) {
        Origins* origins = &origins_table_[unit];
        if (!origins->pieces || origins->piece_count == 0) {
            continue;
        }
        const Piece* pieces = origins->pieces;
        const Piece* first = &pieces[0];
        const Piece* last = &pieces[origins->piece_count - 1];
        u32 span = last->end - first->start;
        if (span == 0) {
            continue;
        }
        // el base del modulo, o 0 para el ejecutable
        u32 base = 0;
        if (unit > 0) {
            for (auto& [mbase, size, index] : placed_) {
                if (index + 1 == unit) {
                    base = mbase;
                    break;
                }
            }
            if (base == 0) {
                continue; // modulo sin colocar: su codigo no esta en memoria
            }
        }
        std::vector<u8> code(span);
        try {
            memory.ReadBlock(base + first->start, code.data(), code.size());
        } catch (...) {
            continue; // memoria no accesible: dejar el codigo como esta
        }
        // cuantas piezas antes de cada una cambiaron
        std::vector<u32> changed_before(origins->piece_count + 1, 0);
        for (std::size_t i = 0; i < origins->piece_count; i++) {
            const Piece& piece = pieces[i];
            CodeHash hash;
            hash.Add(&code[(piece.start - first->start)],
                     piece.end - piece.start);
            changed_before[i + 1] =
                changed_before[i] + (hash.value != piece.hash ? 1u : 0u);
        }
        const Run* runs = origins->runs;
        std::size_t stale = 0;
        for (std::size_t f = 0; f < origins->count; f++) {
            const Origin& function = origins->functions[f];
            u32 end = function.first + function.count;
            bool changed = changed_before[end] > changed_before[function.first];
            origins->stale[f] = changed ? 1 : 0;
            origins->starts[f] = changed ? NO_START : function.start;
            stale += changed;
        }
        stale_total += stale;
        if (stale > 0) {
            LOG_WARNING(Core_ARM11,
                        "{} funciones de la unidad {} cambiaron desde que se "
                        "recompilaron: pasan por el interprete",
                        stale, unit);
        }
    }
    // lo encontrado antes puede haber cambiado
    found_.assign(FOUND_SLOTS, {0xFFFFFFFFu, nullptr});
    return stale_total;
}

void LibraryManager::PlaceModule(const std::string& name, u32 base,
                                 Memory::MemorySystem& memory) {
    if (!Loaded()) {
        return;
    }
    std::size_t index = module_count_;
    for (std::size_t i = 0; i < module_count_; i++) {
        if (name == modules_[i].name) {
            index = i;
            break;
        }
    }
    if (index == module_count_) {
        return; // la libreria no trae codigo de ese modulo
    }
    *modules_[index].base = base;
    std::erase_if(placed_, [&](const auto& placed) {
        return std::get<2>(placed) == index;
    });
    if (base != 0) {
        placed_.emplace_back(base, modules_[index].size, index);
        LOG_INFO(Core_ARM11, "codigo recompilado de {} en 0x{:08X}", name, base);
        // comprobar el codigo recien cargado contra lo recompilado
        CheckStale(memory);
    }
    found_.assign(FOUND_SLOTS, {0xFFFFFFFFu, nullptr});
}

std::string LibraryManager::LibraryPathForTitle(u64 program_id) {
    std::string dir = FileUtil::GetUserPath(FileUtil::UserPath::RecompDir);
    if (dir.empty()) {
        return {};
    }
    return fmt::format("{}{:016X}.so", dir, program_id);
}

} // namespace Core::Recomp
