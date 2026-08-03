//
//  S1Prefix.h
//  AudioKitSynthOne - Linux port
//
//  Force-included into every Synth One translation unit (see the -include flag
//  in CMakeLists.txt), standing in for the transitive includes that Apple's
//  libc++ and Foundation supply on iOS. The upstream sources use std::unique_ptr,
//  std::function and the C string/stdio functions without including the
//  corresponding headers themselves.
//

#pragma once

#ifdef __cplusplus
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cfloat>

// S1Rate.hpp calls unqualified abs() on floats. On Apple platforms the C++
// floating-point overloads are in scope globally, so it does the right thing;
// with libstdc++ the integer ::abs(int) wins instead and silently truncates --
// abs(0.7f) becomes 0, which would break S1Rate's nearest-rate search. Pulling
// the std overloads into the global namespace restores Apple's resolution.
using std::abs;
#endif
