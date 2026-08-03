//
//  AppleTypes.h
//  AudioKitSynthOne - Linux port
//
//  Minimal stand-ins for the CoreAudio / AudioToolbox types the Synth One DSP
//  kernel refers to. Only the pieces actually touched by the kernel are defined;
//  this is not a general CoreAudio emulation layer.
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Objective-C spellings that appear inside otherwise-C++ code
// ---------------------------------------------------------------------------

// `nil` is passed as a null buffer pointer to several sp_*_compute() calls.
#ifndef nil
#define nil nullptr
#endif

// ARC ownership qualifiers are meaningless here but appear on declarations
// and parameters (e.g. `__weak AEArray *`).
#ifndef __weak
#define __weak
#endif
#ifndef __unsafe_unretained
#define __unsafe_unretained
#endif

// ---------------------------------------------------------------------------
// CoreAudio scalar types
// ---------------------------------------------------------------------------

typedef uint32_t UInt32;
typedef int32_t  SInt32;
typedef uint16_t UInt16;
typedef int16_t  SInt16;
typedef uint8_t  UInt8;
typedef long     NSInteger;
typedef unsigned long NSUInteger;

typedef signed char BOOL;
#ifndef YES
#define YES ((BOOL)1)
#endif
#ifndef NO
#define NO ((BOOL)0)
#endif

typedef float    AUValue;
typedef uint64_t AUParameterAddress;
typedef uint32_t AUAudioFrameCount;
typedef int64_t  AUEventSampleTime;

// ---------------------------------------------------------------------------
// AudioBufferList
//
// Synth One renders through `outBufferListPtr->mBuffers[n].mData`, always with
// one non-interleaved float buffer per channel.
// ---------------------------------------------------------------------------

typedef struct AudioBuffer {
    UInt32 mNumberChannels;
    UInt32 mDataByteSize;
    void  *mData;
} AudioBuffer;

typedef struct AudioBufferList {
    UInt32      mNumberBuffers;
    AudioBuffer mBuffers[2];
} AudioBufferList;

// ---------------------------------------------------------------------------
// MIDI events
// ---------------------------------------------------------------------------

typedef struct AUMIDIEvent {
    AUEventSampleTime eventSampleTime;
    uint8_t           length;
    uint8_t           data[3];
} AUMIDIEvent;

// ---------------------------------------------------------------------------
// AudioUnitParameterUnit
//
// Carried in the kernel's S1ParameterInfo table purely as metadata; the Linux
// build never interprets it, but the enumerators must exist.
// ---------------------------------------------------------------------------

typedef enum {
    kAudioUnitParameterUnit_Generic          = 0,
    kAudioUnitParameterUnit_Indexed          = 1,
    kAudioUnitParameterUnit_Boolean          = 2,
    kAudioUnitParameterUnit_Percent          = 3,
    kAudioUnitParameterUnit_Seconds          = 4,
    kAudioUnitParameterUnit_SampleFrames     = 5,
    kAudioUnitParameterUnit_Phase            = 6,
    kAudioUnitParameterUnit_Rate             = 7,
    kAudioUnitParameterUnit_Hertz            = 8,
    kAudioUnitParameterUnit_Cents            = 9,
    kAudioUnitParameterUnit_RelativeSemiTones = 10,
    kAudioUnitParameterUnit_MIDINoteNumber   = 11,
    kAudioUnitParameterUnit_MIDIController   = 12,
    kAudioUnitParameterUnit_Decibels         = 13,
    kAudioUnitParameterUnit_LinearGain       = 14,
    kAudioUnitParameterUnit_Degrees          = 15,
    kAudioUnitParameterUnit_EqualPowerCrossfade = 16,
    kAudioUnitParameterUnit_MixerFaderCurve1 = 17,
    kAudioUnitParameterUnit_Pan              = 18,
    kAudioUnitParameterUnit_Meters           = 19,
    kAudioUnitParameterUnit_AbsoluteCents    = 20,
    kAudioUnitParameterUnit_Octaves          = 21,
    kAudioUnitParameterUnit_BPM              = 22,
    kAudioUnitParameterUnit_Beats            = 23,
    kAudioUnitParameterUnit_Milliseconds     = 24,
    kAudioUnitParameterUnit_Ratio            = 25,
    kAudioUnitParameterUnit_CustomUnit       = 26
} AudioUnitParameterUnit;

// ---------------------------------------------------------------------------
// Helpers that AudioKit's DSPKernel injects into scope
// ---------------------------------------------------------------------------

template <typename T>
static inline T clamp(T input, T low, T high) {
    return std::min(std::max(input, low), high);
}

static inline float pow2(float in) {
    return in * in;
}
