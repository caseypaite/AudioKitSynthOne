//
//  PlatformPaths.cpp
//  AudioKitSynthOne - Linux / Windows port
//

#include "PlatformPaths.h"

#ifdef _WIN32
#include <windows.h>

#include <filesystem>
#endif

namespace s1 {

std::string executableDir() {
#ifdef _WIN32
    // GetModuleFileNameW truncates rather than telling you how much room it
    // wants, and signals that by filling the buffer exactly; grow and retry.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path().string();
#else
    return {};
#endif
}

} // namespace s1
