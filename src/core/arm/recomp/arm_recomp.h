// backend de CPU con codigo recompilado ahead-of-time por 3dsrecomp.
//
// Sin JIT (ARM32 de 32 bits no tiene dynarmic) el emulador iba a interprete
// puro y los juegos corrian lentos en telefonos modestos. Este backend
// ejecuta el codigo que 3dsrecomp genero para el juego actual directamente
// en la CPU del telefono: el codigo huesped recompilado corre a velocidad
// nativa y solo lo que no cubre (menos del 0,3% de las instrucciones en los
// juegos probados) pasa al interprete de respaldo, que es FastInterp.
//
// La ABI con el codigo generado es la del proyecto 3dsrecomp: una tabla de
// funciones por direccion huesped que se ejecutan con un Context de
// registros, accesos directos a traves de la tabla de paginas de Azahar y
// devolucion del control al anfitrion para svc, presupuesto agotado o
// salto a codigo sin cobertura.

#pragma once

#include <memory>

#include "core/arm/arm_interface.h"
#include "core/arm/fastinterp/fastinterp.h"
#include "core/arm/recomp/recomp_abi.h"
#include "core/arm/recomp/recomp_manager.h"
#include "core/arm/skyeye_common/armstate.h"

namespace Memory {
class MemorySystem;
}

namespace Core {
class System;
} // namespace Core

namespace Kernel {
class SVCContext;
}

namespace Core::Recomp {

class ARM_Recomp final : public ARM_Interface {
public:
    ARM_Recomp(Core::System& system, Memory::MemorySystem& memory, u32 id,
               std::shared_ptr<Core::Timing::Timer> timer);
    ~ARM_Recomp() override;

    void Run() override;
    void Step() override;
    void ClearInstructionCache() override;
    void InvalidateCacheRange(u32 start_address, std::size_t length) override;
    void ClearExclusiveState() override;
    void SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) override;

    void SetPC(u32 addr) override;
    u32 GetPC() const override;
    u32 GetReg(int index) const override;
    void SetReg(int index, u32 value) override;
    u32 GetVFPReg(int index) const override;
    void SetVFPReg(int index, u32 value) override;
    u32 GetVFPSystemReg(VFPSystemRegister reg) const override;
    void SetVFPSystemReg(VFPSystemRegister reg, u32 value) override;
    u32 GetCPSR() const override;
    void SetCPSR(u32 cpsr) override;
    u32 GetCP15Register(CP15Register reg) const override;
    void SetCP15Register(CP15Register reg, u32 value) override;

    void SaveContext(ThreadContext& ctx) override;
    void LoadContext(const ThreadContext& ctx) override;
    void PrepareReschedule() override;

    bool HasSingleInstructionBreakAccuracy() override {
        return false;
    }

    /// la libreria del titulo en curso, compartida por todos los nucleos.
    static LibraryManager& GetManager();

    /// intenta abrir la libreria recompilada de un titulo: primero la ruta
    /// que fijo la interfaz Java (ya copiada a almacenamiento privado de la
    /// app) y si no, el directorio recomp del usuario. Devuelve una
    /// descripcion del resultado para el registro.
    static std::string OpenLibraryForTitle(u64 program_id);

    /// si hay libreria cargada (para estadisticas y la interfaz).
    static bool RecompActive();

    /// instrucciones ejecutadas por el codigo recompilado y por el
    /// interprete de respaldo desde que se cargo la libreria.
    static u64 recomp_instructions;
    static u64 fallback_instructions;

protected:
    std::shared_ptr<Memory::PageTable> GetPageTable() const override;

private:
    /// ejecuta una instruccion a traves de FastInterp sincronizando estados.
    /// Devuelve true si la instruccion salto (o fallo) y el anfitrion debe
    /// retomar desde r15.
    bool FallbackStep(u32 address);

    /// respalda todo el estado en FastInterp y corre su Run() entero (para
    /// cuando no hay libreria: el backend se comporta igual que FastInterp).
    void FallbackRun();

    /// sincroniza el Context al estado de FastInterp y de vuelta.
    void SyncToFallback();
    void SyncFromFallback();

    u32 PackCPSR() const;
    void UnpackCPSR(u32 cpsr);

    // callbacks C que el codigo recompilado invoca
    static std::uint8_t HostRead8(Context* ctx, std::uint32_t address);
    static std::uint16_t HostRead16(Context* ctx, std::uint32_t address);
    static std::uint32_t HostRead32(Context* ctx, std::uint32_t address);
    static void HostWrite8(Context* ctx, std::uint32_t address, std::uint8_t value);
    static void HostWrite16(Context* ctx, std::uint32_t address, std::uint16_t value);
    static void HostWrite32(Context* ctx, std::uint32_t address, std::uint32_t value);
    static void HostInterpret(Context* ctx, std::uint32_t address, std::uint32_t opcode);
    static Code HostLookup(Context* ctx, std::uint32_t address);
    static ARM_Recomp* SelfOf(Context* ctx);

    static const Host kHost;

    Memory::MemorySystem& memory_;
    std::shared_ptr<Memory::PageTable> page_table_;
    std::unique_ptr<FastInterp::ARM_FastInterp> fallback_;
    std::unique_ptr<Kernel::SVCContext> svc_context_;

    // el estado visible del nucleo: el codigo generado lo actualiza en
    // cada retorno y los accesores lo leen de aqui.
    Context ctx_{};
    std::uint32_t vfp_regs_[32]{}; // s0..s31 (dN ocupa s2N y s2N+1)
    std::uint32_t fpscr_{};
    std::uint32_t fpexc_{};
    // los bits del cpsr que no son banderas: modo, I, F, E.
    std::uint32_t cpsr_other_{USER32MODE};
    std::uint32_t cp15_uprw_{};
    std::uint32_t cp15_uro_{};

    bool halted_{false};

    /// true mientras el interprete de respaldo esta ejecutando: los
    /// registros vivos estan en su estado y los accesores deben leerlo de
    /// alli (el kernel consulta GetReg/GetPC del nucleo en marcha dentro de
    /// los handlers de svc).
    bool fallback_active_{false};
};

} // namespace Core::Recomp
