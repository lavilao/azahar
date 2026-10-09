// informe de senales nativas a un archivo legible

#pragma once

namespace CrashHandler {

/// instala el gestor de senales y fija la ruta del informe.
void Install(const char* path);

/// desinstala (ruta vacia).
void Clear();

} // namespace CrashHandler
