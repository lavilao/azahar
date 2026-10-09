// SPDX-FileCopyrightText: 2026 Azahar Emulator Project (backend recomp AOT)
// SPDX-License-Identifier: GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/arm/recomp/arm_recomp.h"

#include <cstring>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/arm/skyeye_common/armstate.h"
#include "core/hle/kernel/svc.h"
#include "core/memory.h"

namespace Core::Recomp {

// cuantas instrucciones ejecuto cada lado desde que se cargo la libreria
u64 ARM_Recomp::recomp_instructions = 0;
u64 ARM_Recomp::fallback_instructions = 0;

namespace {
LibraryManager& Manager() {
    static LibraryManager manager;
    return manager;
}
} // namespace

LibraryManager& ARM_Recomp::GetManager() {
    return Manager();
}

bool ARM_Recomp::RecompActive() {
    return Manager().Loaded() && Settings::values.use_recomp.GetValue();
}

std::string ARM_Recomp::OpenLibraryForTitle(u64 program_id) {
    auto& manager = Manager();
    std::string error;

    // primero la ruta que fijo la interfaz Java: la libreria ya copiada al
    // almacenamiento privado de la app, unico sitio del telefono del que
    // dlopen puede cargar (el almacenamiento compartido no es ejecutable).
    const std::string& pending = LibraryManager::pending_library_path;
    if (!pending.empty()) {
        if (manager.Open(pending, error)) {
            return "codigo recompilado: " + manager.Describe();
        }
        LOG_WARNING(Core_ARM11, "la libreria recompilada indicada no se pudo abrir ({}): {}",
                    pending, error);
    }

#ifndef ANDROID
    // en escritorio el directorio del usuario es cargable directamente
    const std::string path = LibraryManager::LibraryPathForTitle(program_id);
    if (!path.empty() && FileUtil::Exists(path)) {
        if (manager.Open(path, error)) {
            return "codigo recompilado: " + manager.Describe();
        }
        LOG_WARNING(Core_ARM11, "no se pudo abrir {} : {}", path, error);
    }
#endif

    return {};
}

const Host ARM_Recomp::kHost = {
    &ARM_Recomp::HostRead8,   &ARM_Recomp::HostRead16,  &ARM_Recomp::HostRead32,
    &ARM_Recomp::HostWrite8,  &ARM_Recomp::HostWrite16, &ARM_Recomp::HostWrite32,
    &ARM_Recomp::HostInterpret, &ARM_Recomp::HostLookup,
};

ARM_Recomp::ARM_Recomp(Core::System& system, Memory::MemorySystem& memory, u32 id,
                       std::shared_ptr<Timing::Timer> timer)
    : ARM_Interface(id, timer), memory_(memory),
      fallback_(std::make_unique<FastInterp::ARM_FastInterp>(system, memory, id, timer)),
      svc_context_(std::make_unique<Kernel::SVCContext>(system)) {

    ctx_.vfp = vfp_regs_;
    ctx_.fpscr = &fpscr_;
    ctx_.host = &kHost;
    ctx_.user = this;
    ctx_.r[15] = 0;
}

ARM_Recomp::~ARM_Recomp() = default;

// ---------------------------------------------------------------------------
// ejecucion
// ---------------------------------------------------------------------------

void ARM_Recomp::Run() {
    halted_ = false;

    if (break_flag || !Manager().Loaded() || !Settings::values.use_recomp.GetValue()) {
        // sin libreria este backend es FastInterp: mismo comportamiento,
        // coste de sincronizacion minimo (una vez por llamada).
        FallbackRun();
        return;
    }

    ctx_.read_pages = page_table_ ? page_table_->GetPointerArray().data() : nullptr;
    ctx_.write_pages = ctx_.read_pages;
    ctx_.tls = cp15_uro_;

    while (!halted_ && !break_flag && timer->GetDowncount() > 0) {
        const u32 pc = ctx_.r[15];

        // PC en memoria sin mapear: consumir la rodaja como hace FastInterp
        if (!ctx_.read_pages || !ctx_.read_pages[pc >> 12]) {
            static u32 unmapped = 0;
            if (unmapped++ < 5) {
                LOG_ERROR(Core_ARM11, "PC {:08X} en memoria sin mapear (thumb={}, lr={:08X})", pc,
                          ctx_.thumb, ctx_.r[14]);
            }
            timer->AddTicks(static_cast<u64>(std::max<s64>(timer->GetDowncount(), 0)));
            break;
        }

        const Code code = Manager().Lookup(pc | ctx_.thumb);
        if (!code) {
            // sin cobertura para este PC: una instruccion por el interprete
            FallbackStep(pc);
            fallback_instructions++;
            continue;
        }

        // presupuesto de instrucciones de esta corrida: acotado para que
        // los eventos agendados a mitad de rodaja se respeten pronto
        const s64 slice = timer->GetDowncount();
        const s32 budget = static_cast<s32>(std::min<s64>(slice, 2048));
        ctx_.budget = budget;
        ctx_.exit = EXIT_NONE;
        ctx_.depth = 0;

        code(&ctx_);

        const s32 ran = budget - std::max(ctx_.budget, 0);
        recomp_instructions += static_cast<u64>(std::max(ran, 0));

        switch (ctx_.exit) {
        case EXIT_SVC:
            // las instrucciones hasta el svc ya corrieron; el handler suma
            // sus propios ciclos y puede cambiar de hilo (LoadContext sobre
            // este nucleo actualiza ctx_ de inmediato)
            if (ran > 0) {
                timer->AddTicks(ran);
            }
            svc_context_->CallSVC(ctx_.svc);
            break;

        case EXIT_BUDGET:
        case EXIT_NONE:
            if (ran > 0) {
                timer->AddTicks(ran);
            }
            break;

        case EXIT_UNWIND:
        default:
            // el codigo devolvio el control: r15 apunta a donde seguir (una
            // instruccion interpretada dentro de un callback salto, o un
            // destino sin cobertura): el bucle decide por donde continuar
            if (ran > 0) {
                timer->AddTicks(ran);
            }
            break;
        }
    }
}

bool ARM_Recomp::FallbackStep(u32 address) {
    // el estado vive en ctx_; FastInterp trabaja con el suyo: sincronizar,
    // pisar el PC, dar un paso y traer de vuelta el resultado
    ARM_Interface::ThreadContext tc{};
    std::memcpy(tc.cpu_registers.data(), ctx_.r, sizeof(ctx_.r));
    tc.cpsr = PackCPSR();
    for (int i = 0; i < 32; i++) {
        tc.fpu_registers[i] = vfp_regs_[i];
    }
    tc.fpscr = fpscr_;
    tc.fpexc = fpexc_;

    fallback_->LoadContext(tc);
    fallback_->SetPC(address);
    fallback_active_ = true;
    fallback_->StepOne();
    fallback_active_ = false;
    fallback_->SaveContext(tc);

    std::memcpy(ctx_.r, tc.cpu_registers.data(), sizeof(ctx_.r));
    UnpackCPSR(tc.cpsr);
    for (int i = 0; i < 32; i++) {
        vfp_regs_[i] = tc.fpu_registers[i];
    }
    fpscr_ = tc.fpscr;
    fpexc_ = tc.fpexc;

    // salto (o fallo) si el PC no quedo justo despues de la instruccion
    u32 expected;
    if (!ctx_.thumb) {
        expected = address + 4;
    } else {
        u32 halfword = 0;
        const u8* page = ctx_.read_pages ? ctx_.read_pages[address >> 12] : nullptr;
        if (page) {
            std::memcpy(&halfword, page + (address & 0xFFF), 2);
        } else {
            halfword = memory_.Read16(address);
        }
        const bool wide = (halfword & 0xE000) == 0xE000 && (halfword & 0x1800) != 0;
        expected = address + (wide ? 4 : 2);
    }
    return ctx_.r[15] != expected;
}

void ARM_Recomp::FallbackRun() {
    SyncToFallback();
    fallback_active_ = true;
    fallback_->Run();
    fallback_active_ = false;
    SyncFromFallback();
}

void ARM_Recomp::Step() {
    if (break_flag) [[unlikely]] {
        return;
    }
    SyncToFallback();
    fallback_active_ = true;
    fallback_->StepOne();
    fallback_active_ = false;
    SyncFromFallback();
}

// ---------------------------------------------------------------------------
// estado visible
// ---------------------------------------------------------------------------

void ARM_Recomp::SetPC(u32 addr) {
    ctx_.r[15] = addr;
    fallback_->SetPC(addr);
}

u32 ARM_Recomp::GetPC() const {
    return fallback_active_ ? fallback_->GetPC() : ctx_.r[15];
}

u32 ARM_Recomp::GetReg(int index) const {
    return fallback_active_ ? fallback_->GetReg(index) : ctx_.r[index];
}

void ARM_Recomp::SetReg(int index, u32 value) {
    ctx_.r[index] = value;
    fallback_->SetReg(index, value);
}

u32 ARM_Recomp::GetVFPReg(int index) const {
    return fallback_active_ ? fallback_->GetVFPReg(index) : vfp_regs_[index];
}

void ARM_Recomp::SetVFPReg(int index, u32 value) {
    vfp_regs_[index] = value;
    fallback_->SetVFPReg(index, value);
}

u32 ARM_Recomp::GetVFPSystemReg(VFPSystemRegister reg) const {
    if (fallback_active_) {
        return fallback_->GetVFPSystemReg(reg);
    }
    switch (reg) {
    case VFP_FPSCR:
        return fpscr_;
    case VFP_FPEXC:
        return fpexc_;
    default:
        return 0;
    }
}

void ARM_Recomp::SetVFPSystemReg(VFPSystemRegister reg, u32 value) {
    fallback_->SetVFPSystemReg(reg, value);
    switch (reg) {
    case VFP_FPSCR:
        fpscr_ = value;
        break;
    case VFP_FPEXC:
        fpexc_ = value;
        break;
    default:
        break;
    }
}

u32 ARM_Recomp::PackCPSR() const {
    u32 cpsr = 0;
    cpsr |= ctx_.n << 31;
    cpsr |= ctx_.z << 30;
    cpsr |= ctx_.c << 29;
    cpsr |= ctx_.v << 28;
    cpsr |= ctx_.q << 27;
    cpsr |= (ctx_.ge & 0xF) << 16;
    cpsr |= ctx_.thumb << 5;
    // modo, I, F, E y el resto se conservan tal cual
    return cpsr | cpsr_other_;
}

void ARM_Recomp::UnpackCPSR(u32 cpsr) {
    ctx_.n = (cpsr >> 31) & 1;
    ctx_.z = (cpsr >> 30) & 1;
    ctx_.c = (cpsr >> 29) & 1;
    ctx_.v = (cpsr >> 28) & 1;
    ctx_.q = (cpsr >> 27) & 1;
    ctx_.ge = (cpsr >> 16) & 0xF;
    ctx_.thumb = (cpsr >> 5) & 1;
    // conservar modo, I, F, E; sin modo explicito, usuario
    constexpr u32 flags_mask = 0xF9000000u | 0x000F0000u | 0x20u;
    u32 other = cpsr & ~flags_mask;
    if ((other & 0x1F) == 0) {
        other |= USER32MODE;
    }
    cpsr_other_ = other;
}

u32 ARM_Recomp::GetCPSR() const {
    return fallback_active_ ? fallback_->GetCPSR() : PackCPSR();
}

void ARM_Recomp::SetCPSR(u32 cpsr) {
    UnpackCPSR(cpsr);
}

u32 ARM_Recomp::GetCP15Register(CP15Register reg) const {
    if (fallback_active_) {
        return fallback_->GetCP15Register(reg);
    }
    switch (reg) {
    case CP15_THREAD_UPRW:
        return cp15_uprw_;
    case CP15_THREAD_URO:
        return cp15_uro_;
    default:
        return 0;
    }
}

void ARM_Recomp::SetCP15Register(CP15Register reg, u32 value) {
    // el respaldo tambien consulta cp15 (mrc de la TLS): mantenerlo al dia
    fallback_->SetCP15Register(reg, value);
    switch (reg) {
    case CP15_THREAD_UPRW:
        cp15_uprw_ = value;
        break;
    case CP15_THREAD_URO:
        // el kernel escribe aqui la TLS del hilo al cambiar de contexto
        cp15_uro_ = value;
        ctx_.tls = value;
        break;
    default:
        break;
    }
}

void ARM_Recomp::SaveContext(ThreadContext& tc) {
    // durante la ejecucion del respaldo los registros vivos son los suyos
    // (el kernel salva el contexto del hilo en marcha dentro de un svc)
    if (fallback_active_) {
        fallback_->SaveContext(tc);
        return;
    }
    std::memcpy(tc.cpu_registers.data(), ctx_.r, sizeof(ctx_.r));
    tc.cpsr = PackCPSR();
    for (int i = 0; i < 32; i++) {
        tc.fpu_registers[i] = vfp_regs_[i];
    }
    tc.fpscr = fpscr_;
    tc.fpexc = fpexc_;
}

void ARM_Recomp::LoadContext(const ThreadContext& tc) {
    std::memcpy(ctx_.r, tc.cpu_registers.data(), sizeof(ctx_.r));
    UnpackCPSR(tc.cpsr);
    for (int i = 0; i < 32; i++) {
        vfp_regs_[i] = tc.fpu_registers[i];
    }
    fpscr_ = tc.fpscr;
    fpexc_ = tc.fpexc;
    // el respaldo queda sincronizado para cualquier uso directo
    fallback_->LoadContext(tc);
}

void ARM_Recomp::PrepareReschedule() {
    halted_ = true;
    fallback_->PrepareReschedule();
}

void ARM_Recomp::ClearInstructionCache() {
    // el juego pudo reescribir su propio codigo: comprobar que lo
    // recompilado sigue igual y marcar lo cambiado para el interprete
    if (Manager().Loaded()) {
        Manager().CheckStale(memory_);
    }
    fallback_->ClearInstructionCache();
}

void ARM_Recomp::InvalidateCacheRange(u32 start_address, std::size_t length) {
    ClearInstructionCache();
}

void ARM_Recomp::ClearExclusiveState() {
    ctx_.exclusive = 0;
    fallback_->ClearExclusiveState();
}

void ARM_Recomp::SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) {
    if (page_table_ == page_table) {
        return;
    }
    page_table_ = page_table;
    fallback_->SetPageTable(page_table);
}

std::shared_ptr<Memory::PageTable> ARM_Recomp::GetPageTable() const {
    return page_table_;
}

void ARM_Recomp::SyncToFallback() {
    ARM_Interface::ThreadContext tc{};
    SaveContext(tc);
    fallback_->LoadContext(tc);
}

void ARM_Recomp::SyncFromFallback() {
    ARM_Interface::ThreadContext tc{};
    fallback_->SaveContext(tc);
    // conservar los registros cp15 de Azahar
    LoadContext(tc);
}

// ---------------------------------------------------------------------------
// callbacks que el codigo recompilado invoca
// ---------------------------------------------------------------------------

ARM_Recomp* ARM_Recomp::SelfOf(Context* ctx) {
    return static_cast<ARM_Recomp*>(ctx->user);
}

std::uint8_t ARM_Recomp::HostRead8(Context* ctx, std::uint32_t address) {
    return SelfOf(ctx)->memory_.Read8(address);
}

std::uint16_t ARM_Recomp::HostRead16(Context* ctx, std::uint32_t address) {
    return SelfOf(ctx)->memory_.Read16(address);
}

std::uint32_t ARM_Recomp::HostRead32(Context* ctx, std::uint32_t address) {
    return SelfOf(ctx)->memory_.Read32(address);
}

void ARM_Recomp::HostWrite8(Context* ctx, std::uint32_t address, std::uint8_t value) {
    SelfOf(ctx)->memory_.Write8(address, value);
}

void ARM_Recomp::HostWrite16(Context* ctx, std::uint32_t address, std::uint16_t value) {
    SelfOf(ctx)->memory_.Write16(address, value);
}

void ARM_Recomp::HostWrite32(Context* ctx, std::uint32_t address, std::uint32_t value) {
    SelfOf(ctx)->memory_.Write32(address, value);
}

void ARM_Recomp::HostInterpret(Context* ctx, std::uint32_t address, std::uint32_t /*opcode*/) {
    Context& c = *ctx;
    ARM_Recomp* self = SelfOf(ctx);
    fallback_instructions++;
    if (self->FallbackStep(address)) {
        // salto o fallo: el anfitrion retoma desde r15
        c.exit = EXIT_UNWIND;
    }
}

Code ARM_Recomp::HostLookup(Context* ctx, std::uint32_t address) {
    return Manager().Lookup(address);
}

} // namespace Core::Recomp
