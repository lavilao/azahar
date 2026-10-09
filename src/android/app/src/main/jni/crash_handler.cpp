// informe de senales nativas (SIGSEGV, SIGABRT, ...) a un archivo legible,
// para diagnosticar cierres a penas arrancar sin depender de logcat.
//
// El gestor se instala desde JNI_OnLoad con la ruta que la interfaz Java le
// pasa (almacenamiento externo especifico de la app). Dentro del gestor solo
// se usan llamadas seguras ante senales (snprintf/open/write) y luego se
// restaura el comportamiento por defecto para que el proceso muera con su
// tombstone habitual.

#include "jni/crash_handler.h"

#include <android/api-level.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>

namespace CrashHandler {

// la ruta del informe, fijada por la interfaz Java antes de que pase nada
static char report_path[4096];

static void WriteReport(int signal, siginfo_t* info, void* context) {
    if (report_path[0] == '\0') {
        return;
    }

    ucontext_t* uc = static_cast<ucontext_t*>(context);
    char buffer[1024];

    const unsigned pc = uc->uc_mcontext.arm_pc;
    const unsigned lr = uc->uc_mcontext.arm_lr;
    const unsigned sp = uc->uc_mcontext.arm_sp;

    int length = std::snprintf(
        buffer, sizeof(buffer),
        "senal %d en pc=0x%08X lr=0x%08X sp=0x%08X direccion=0x%lx api=%d\n",
        signal, pc, lr, sp, reinterpret_cast<unsigned long>(info->si_addr),
        android_get_device_api_level());

    const int fd = open(report_path, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0666);
    if (fd >= 0) {
        if (length > 0) {
            ssize_t written = write(fd, buffer, static_cast<size_t>(length));
            (void)written;
        }
        close(fd);
    }

    // restaurar el gestor por defecto y volver a lanzar: el proceso muere
    // como lo habria hecho, con su tombstone para adb logcat
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigaction(signal, &action, nullptr);
    raise(signal);
}

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
