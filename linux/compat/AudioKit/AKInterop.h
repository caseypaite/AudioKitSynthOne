//
//  AKInterop.h
//  AudioKitSynthOne - Linux port
//
//  Stands in for AudioKit's AKInterop.h. Reproduces the non-Objective-C branch
//  of AK_ENUM so S1Parameter.h declares the same fixed-underlying-type enum it
//  does on Apple platforms.
//

#pragma once

#ifdef __cplusplus
#define AK_API extern "C"
#else
#define AK_API
#endif

#include <cstdint>

// An enum with a fixed underlying type. This spelling satisfies both callers:
//
//   S1Parameter.h:     typedef AK_ENUM(S1Parameter) { ... };
//   AKSynthOneRate.h:  typedef AK_ENUM(AKSynthOneRate) { ... } AKSynthOneRate;
//
// The first form is an unnamed typedef (harmless; -Wmissing-declarations is
// disabled for these sources), the second a conventional one. In C++ the enum
// name is usable directly, so both end up with a usable `S1Parameter` /
// `AKSynthOneRate` type name.
#define AK_ENUM(_type) enum _type : int32_t
