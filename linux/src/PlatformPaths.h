//
//  PlatformPaths.h
//  AudioKitSynthOne - Linux / Windows port
//
//  The few filesystem questions that only the OS can answer.
//
//  This exists as its own translation unit so that <windows.h> never meets the
//  compat layer: compat/AppleTypes.h defines Apple's `typedef signed char BOOL`
//  and <windows.h> defines `typedef int BOOL`, which is a hard conflict in any
//  file that includes both. (<windows.h> also defines min/max as macros, which
//  would break std::min in the DSP sources.) Keeping the Win32 surface here
//  means Engine.cpp can stay a plain C++ file.
//

#pragma once

#include <string>

namespace s1 {

/// The directory holding the running executable, or empty when the platform
/// does not provide it. Only Windows needs this: the Linux build finds its
/// resources through paths compiled in at configure time and the wrapper
/// scripts install.sh writes.
std::string executableDir();

} // namespace s1
