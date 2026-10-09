// informe de senales nativas (SIGSEGV, SIGABRT, ...) a un archivo legible,
// para diagnosticar cierres a penas arrancar sin depender de logcat.
//
// El gestor se instala desde JNI_OnLoad con la ruta que la interfaz Java le
// pasa (almacenamiento externo especifico de la app). Dentro del gestor solo
// se usan llamadas seguras ante senales (open/read/pread/write/close y
// snprintf de bionic) y luego se restaura el comportamiento por defecto para
// que el proceso muera con su tombstone habitual.
//
// v2: el informe ahora incluye TODO lo necesario para localizar el fallo sin
// adb: hilo, registros completos, si_code, los bytes de la instruccion en pc,
// el volcado de los mapeos ejecutables del proceso y pc/fallo expresados como
// biblioteca+offset de archivo (simbolizables con llvm-symbolizer).

#include "jni/crash_handler.h"

#include <android/api-level.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ucontext.h>

namespace CrashHandler {

// la ruta del informe, fijada por la interfaz Java antes de que pase nada
static char report_path[4096];

// evita informes recursivos si el propio gestor falla
static volatile sig_atomic_t in_handler = 0;

namespace {

struct Writer {
    int fd;
    bool ok;
};

void WWrite(Writer& w, const char* text) {
    if (!w.ok) {
        return;
    }
    const size_t len = std::strlen(text);
    if (write(w.fd, text, len) < 0) {
        w.ok = false;
    }
}

void WPrintf(Writer& w, const char* fmt, ...) {
    if (!w.ok) {
        return;
    }
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    const int len = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len > 0 && write(w.fd, buffer, static_cast<size_t>(len)) < 0) {
        w.ok = false;
    }
}

const char* SignalName(int signal) {
    switch (signal) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    default:
        return "?";
    }
}

// traduccion de si_code para senales SIGBUS/SIGSEGV
void DescribeCode(Writer& w, int signal, int code) {
    const char* name = "?";
    if (signal == SIGBUS) {
        switch (code) {
        case 1:
            name = "BUS_ADRALN (acceso sin alinear)";
            break;
        case 2:
            name = "BUS_ADRERR (direccion fisica inexistente)";
            break;
        case 3:
            name = "BUS_OBJERR";
            break;
        case 0x80:
            name = "SI_KERNEL (fallo del nucleo)";
            break;
        }
    } else if (signal == SIGSEGV) {
        switch (code) {
        case 1:
            name = "SEGV_MAPERR (direccion sin mapear)";
            break;
        case 2:
            name = "SEGV_ACCERR (permisos)";
            break;
        case 0x80:
            name = "SI_KERNEL";
            break;
        }
    }
    WPrintf(w, "codigo_si=%d (%s)\n", code, name);
}

// ---------------------------------------------------------------------------
// analisis de /proc/self/maps con un parser reentrante sin malloc
// ---------------------------------------------------------------------------

struct Mapping {
    unsigned long start;
    unsigned long end;
    unsigned long file_offset;
    bool executable;
    bool readable;
    bool writable;
    char name[96];
};

// parser manual de una linea de maps: sin malloc ni bloqueos (seguro dentro
// de un gestor de senales). Formato: start-end perms offset dev inode name
bool ParseMapLine(const char* line, Mapping& m) {
    // start-end
    char* end_p = nullptr;
    m.start = std::strtoul(line, &end_p, 16);
    if (end_p == nullptr || *end_p != '-') {
        return false;
    }
    m.end = std::strtoul(end_p + 1, &end_p, 16);
    if (end_p == nullptr || *end_p != ' ') {
        return false;
    }
    // perms (rwxp)
    const char* perms = end_p + 1;
    if (perms[0] == '\0' || perms[1] == '\0' || perms[2] == '\0' || perms[3] == '\0') {
        return false;
    }
    m.readable = perms[0] == 'r';
    m.writable = perms[1] == 'w';
    m.executable = perms[2] == 'x';
    // saltar hasta el offset
    const char* p = perms + 4;
    while (*p == ' ') {
        p++;
    }
    m.file_offset = std::strtoul(p, &end_p, 16);
    // saltar dev e inode hasta el nombre (o fin de linea)
    p = end_p;
    int spaces = 0;
    while (*p != '\0') {
        if (*p == ' ' && p[-1] != ' ') {
            spaces++;
        }
        p++;
        if (spaces == 3) {
            break;
        }
    }
    if (spaces == 3) {
        std::strncpy(m.name, p, sizeof(m.name) - 1);
        m.name[sizeof(m.name) - 1] = '\0';
    } else {
        m.name[0] = '\0';
    }
    return true;
}

// lee /proc/self/maps y guarda solo lo que interesa: mapeos ejecutables y
// los que contienen las direcciones del informe (pc, lr, fallo, sp). El
// archivo puede tener cientos de lineas (monton de Java incluido) y las
// bibliotecas de la app estan al final.
int ReadMaps(Mapping* out, int max, const unsigned long* wanted, int wanted_count) {
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    char line[192];
    int count = 0;
    int line_len = 0;
    char ch;
    bool stop = false;
    while (!stop) {
        const ssize_t n = read(fd, &ch, 1);
        if (n <= 0) {
            stop = true;
            ch = '\n';
        }
        if (ch != '\n') {
            if (line_len < static_cast<int>(sizeof(line)) - 1) {
                line[line_len++] = ch;
            }
            continue;
        }
        line[line_len] = '\0';
        if (line_len > 0) {
            Mapping m{};
            if (ParseMapLine(line, m)) {
                bool keep = m.executable;
                if (!keep) {
                    for (int i = 0; i < wanted_count; i++) {
                        if (wanted[i] >= m.start && wanted[i] < m.end) {
                            keep = true;
                            break;
                        }
                    }
                }
                if (keep && count < max) {
                    out[count++] = m;
                }
            }
        }
        line_len = 0;
    }
    close(fd);
    return count;
}

const Mapping* FindMapping(const Mapping* maps, int count, unsigned long addr) {
    for (int i = 0; i < count; i++) {
        if (addr >= maps[i].start && addr < maps[i].end) {
            return &maps[i];
        }
    }
    return nullptr;
}

// offset de archivo de una direccion dentro de un mapeo (simbolizable)
unsigned long FileOffsetOf(const Mapping& m, unsigned long addr) {
    return m.file_offset + (addr - m.start);
}

void PrintAddress(Writer& w, const Mapping* maps, int count, unsigned long addr) {
    const Mapping* m = FindMapping(maps, count, addr);
    if (m == nullptr) {
        WPrintf(w, "0x%08llX (sin mapear)\n", static_cast<unsigned long long>(addr));
        return;
    }
    if (m->name[0] == '\0') {
        WPrintf(w, "0x%08llX (anonimo %s, offset 0x%llX)\n",
                static_cast<unsigned long long>(addr), m->executable ? "ejecutable" : "datos",
                static_cast<unsigned long long>(FileOffsetOf(*m, addr)));
    } else {
        WPrintf(w, "0x%08llX = %s+0x%llX\n", static_cast<unsigned long long>(addr), m->name,
                static_cast<unsigned long long>(FileOffsetOf(*m, addr)));
    }
}

// lee hasta 8 bytes de memoria del propio proceso sin riesgo de fallo
bool SafeRead(unsigned long addr, unsigned char* out, int len) {
    const int fd = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const ssize_t n = pread(fd, out, len, static_cast<off_t>(addr));
    close(fd);
    return n == len;
}

void WriteReport(int signal, siginfo_t* info, void* context) {
    if (report_path[0] == '\0' || in_handler) {
        return;
    }
    in_handler = 1;

    ucontext_t* uc = static_cast<ucontext_t*>(context);
#if defined(__arm__)
    const unsigned pc = uc->uc_mcontext.arm_pc;
    const unsigned lr = uc->uc_mcontext.arm_lr;
    const unsigned sp = uc->uc_mcontext.arm_sp;
    const unsigned cpsr = uc->uc_mcontext.arm_cpsr;
    const bool thumb = (cpsr & 0x20) != 0;
#elif defined(__aarch64__)
    const unsigned long pc = uc->uc_mcontext.pc;
    const unsigned long lr = uc->uc_mcontext.regs[30];
    const unsigned long sp = uc->uc_mcontext.sp;
    const unsigned cpsr = static_cast<unsigned>(uc->uc_mcontext.pstate);
    const bool thumb = (cpsr & 0x20) != 0;
#elif defined(__i386__)
    const unsigned long pc = uc->uc_mcontext.gregs[REG_EIP];
    const unsigned long lr = uc->uc_mcontext.gregs[REG_EBP];
    const unsigned long sp = uc->uc_mcontext.gregs[REG_ESP];
    const unsigned cpsr = uc->uc_mcontext.gregs[REG_EFL];
    const bool thumb = false;
#elif defined(__x86_64__)
    const unsigned long pc = uc->uc_mcontext.gregs[REG_RIP];
    const unsigned long lr = uc->uc_mcontext.gregs[REG_RBP];
    const unsigned long sp = uc->uc_mcontext.gregs[REG_RSP];
    const unsigned cpsr = 0;
    const bool thumb = false;
#else
    const unsigned long pc = 0, lr = 0, sp = 0, cpsr = 0;
    const bool thumb = false;
#endif
    const unsigned long fault_addr = reinterpret_cast<unsigned long>(info->si_addr);

    Mapping maps[96];
    const unsigned long wanted[4] = {pc, lr, fault_addr, sp};
    const int map_count = ReadMaps(maps, 96, wanted, 4);

    const int fd = open(report_path, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0666);
    Writer w{fd, fd >= 0};

    WPrintf(w, "=== senal %d (%s) ===\n", signal, SignalName(signal));

    // hilo que fallo
    char thread_name[20] = {0};
    const int tfd = open("/proc/thread-self/comm", O_RDONLY | O_CLOEXEC);
    if (tfd >= 0) {
        const ssize_t n = read(tfd, thread_name, sizeof(thread_name) - 1);
        if (n > 0 && thread_name[n - 1] == '\n') {
            thread_name[n - 1] = '\0';
        }
        close(tfd);
    }
    WPrintf(w, "hilo=%s\n", thread_name);

    DescribeCode(w, signal, info->si_code);

    WPrintf(w, "pc=0x%08llX lr=0x%08llX sp=0x%08llX cpsr=0x%08X (modo %s)\n",
            static_cast<unsigned long long>(pc), static_cast<unsigned long long>(lr),
            static_cast<unsigned long long>(sp), cpsr, thumb ? "thumb" : "arm");
    WPrintf(w, "direccion_fallo=0x%08llX\n", static_cast<unsigned long long>(fault_addr));

    // bytes de la instruccion que fallo (via /proc/self/mem, sin riesgo)
    if (FindMapping(maps, map_count, pc) != nullptr) {
        unsigned char inst[8];
        if (SafeRead(pc, inst, 8)) {
            WPrintf(w, "instruccion@pc: %02X %02X %02X %02X | %02X %02X %02X %02X\n", inst[0],
                    inst[1], inst[2], inst[3], inst[4], inst[5], inst[6], inst[7]);
        }
    }

#if defined(__arm__)
    WPrintf(w, "r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X\n", uc->uc_mcontext.arm_r0,
            uc->uc_mcontext.arm_r1, uc->uc_mcontext.arm_r2, uc->uc_mcontext.arm_r3);
    WPrintf(w, "r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X\n", uc->uc_mcontext.arm_r4,
            uc->uc_mcontext.arm_r5, uc->uc_mcontext.arm_r6, uc->uc_mcontext.arm_r7);
    WPrintf(w, "r8=0x%08X r9=0x%08X r10=0x%08X r11=0x%08X r12=0x%08X\n", uc->uc_mcontext.arm_r8,
            uc->uc_mcontext.arm_r9, uc->uc_mcontext.arm_r10, uc->uc_mcontext.arm_fp,
            uc->uc_mcontext.arm_ip);
#elif defined(__aarch64__)
    for (int i = 0; i < 31; i += 4) {
        WPrintf(w, "x%02d=0x%016llX x%02d=0x%016llX x%02d=0x%016llX x%02d=0x%016llX\n", i,
                static_cast<unsigned long long>(uc->uc_mcontext.regs[i]), i + 1,
                static_cast<unsigned long long>(uc->uc_mcontext.regs[i + 1]), i + 2,
                static_cast<unsigned long long>(uc->uc_mcontext.regs[i + 2]), i + 3,
                static_cast<unsigned long long>(uc->uc_mcontext.regs[i + 3]));
    }
#endif

    WWrite(w, "pc en: ");
    PrintAddress(w, maps, map_count, pc);
    WWrite(w, "lr en: ");
    PrintAddress(w, maps, map_count, lr);
    WWrite(w, "fallo en: ");
    PrintAddress(w, maps, map_count, fault_addr);

    // pseudo-backtrace: recorrer la pila buscando direcciones de retorno
    // (direcciones que caen en mapeos ejecutables con nombre)
    WWrite(w, "backtrace aproximado (palabras de pila en mapeos ejecutables):\n");
    unsigned char stack[512];
    if (SafeRead(sp, stack, sizeof(stack))) {
        int printed = 0;
        for (int off = 0;
             off + sizeof(unsigned long) <= sizeof(stack) && printed < 16;
             off += static_cast<int>(sizeof(unsigned long))) {
            const unsigned long val = *reinterpret_cast<unsigned long*>(stack + off);
            const Mapping* m = FindMapping(maps, map_count, val);
            if (m != nullptr && m->executable && m->name[0] != '\0') {
                WPrintf(w, "  [sp+0x%03X] ", off);
                PrintAddress(w, maps, map_count, val);
                printed++;
            }
        }
    } else {
        WWrite(w, "  (pila no legible)\n");
    }

    // volcado de todos los mapeos ejecutables con nombre: identifica que
    // bibliotecas nativas estan cargadas y donde
    WWrite(w, "mapeos ejecutables:\n");
    for (int i = 0; i < map_count; i++) {
        if (maps[i].executable && maps[i].name[0] != '\0') {
            WPrintf(w, "  %08llX-%08llX %s+0x%llX\n", static_cast<unsigned long long>(maps[i].start),
                    static_cast<unsigned long long>(maps[i].end), maps[i].name,
                    static_cast<unsigned long long>(maps[i].file_offset));
        }
    }

    WPrintf(w, "api=%d\n\n", android_get_device_api_level());

    if (fd >= 0) {
        close(fd);
    }
    in_handler = 0;

    // restaurar el gestor por defecto y volver a lanzar: el proceso muere
    // como lo habria hecho, con su tombstone para adb logcat
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigaction(signal, &action, nullptr);
    raise(signal);
}

} // namespace

void Install(const char* path) {
    if (path == nullptr) {
        return;
    }
    std::strncpy(report_path, path, sizeof(report_path) - 1);
    report_path[sizeof(report_path) - 1] = '\0';

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = WriteReport;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (int signal : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
        sigaction(signal, &action, nullptr);
    }
}

void Clear() {
    report_path[0] = '\0';
}

} // namespace CrashHandler
