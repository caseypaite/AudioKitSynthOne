//
//  AEArray.h
//  AudioKitSynthOne - Linux port
//
//  Portable replacement for TheAmazingAudioEngine's AEArray, covering only the
//  way Synth One uses it: a list of held NoteNumbers written from the control
//  thread and read, lock-free and allocation-free, from the render thread.
//
//  Upstream AEArray publishes a new mapped array and defers the release of the
//  old one. Here the storage is a fixed ring of preallocated generations, so
//  publishing never allocates and a reader holding a token keeps looking at a
//  consistent snapshot. With kGenerations generations a reader would have to be
//  descheduled across that many note on/off events before its snapshot could be
//  overwritten.
//
//  The macro and function names match upstream so the unmodified kernel and
//  sequencer sources compile against this header.
//

#pragma once

#include <atomic>
#include <vector>
#include <cstddef>

#include "AppleTypes.h"
#include "S1AudioUnit.h"

typedef const void *AEArrayToken;

class AEArray {
public:
    static constexpr int kMaxItems    = S1_NUM_MIDI_NOTES;
    static constexpr int kGenerations = 4;

    struct Generation {
        NoteNumber items[kMaxItems];
        int        count = 0;
    };

    AEArray() {
        for (int g = 0; g < kGenerations; ++g) {
            generations[g].count = 0;
        }
        live.store(&generations[0], std::memory_order_release);
    }

    /// Implicit decay so a by-value member still satisfies the `AEArray *`
    /// parameters and macros in the unmodified upstream sources.
    operator AEArray *() { return this; }

    /// Publish a new snapshot. Control thread only.
    void update(const std::vector<NoteNumber> &items) {
        writeIndex = (writeIndex + 1) % kGenerations;
        Generation &g = generations[writeIndex];

        const int n = static_cast<int>(items.size() < static_cast<size_t>(kMaxItems)
                                           ? items.size()
                                           : static_cast<size_t>(kMaxItems));
        for (int i = 0; i < n; ++i) {
            g.items[i] = items[i];
        }
        g.count = n;

        live.store(&g, std::memory_order_release);
        count = n;
    }

    void removeAllObjects() {
        writeIndex = (writeIndex + 1) % kGenerations;
        Generation &g = generations[writeIndex];
        g.count = 0;
        live.store(&g, std::memory_order_release);
        count = 0;
    }

    const Generation *token() const { return live.load(std::memory_order_acquire); }

    /// Mirrors upstream's `count` property. Written on publish, read from both
    /// threads; only ever compared against 0, as it is upstream.
    int count = 0;

private:
    Generation                     generations[kGenerations];
    std::atomic<const Generation *> live{nullptr};
    int                            writeIndex = 0;
};

// ---------------------------------------------------------------------------
// Render-thread accessors
// ---------------------------------------------------------------------------

static inline AEArrayToken AEArrayGetToken(AEArray *array) {
    return static_cast<AEArrayToken>(array->token());
}

static inline int AEArrayGetCount(AEArrayToken token) {
    return static_cast<const AEArray::Generation *>(token)->count;
}

static inline void *AEArrayGetItem(AEArrayToken token, int index) {
    const AEArray::Generation *g = static_cast<const AEArray::Generation *>(token);
    if (index < 0 || index >= g->count) {
        return nullptr;
    }
    return const_cast<NoteNumber *>(&g->items[index]);
}

// ---------------------------------------------------------------------------
// Enumeration macro -- same shape and semantics as upstream's.
// ---------------------------------------------------------------------------

#define __AEArrayVar2(x, y) x##y
#define __AEArrayVar1(x, y) __AEArrayVar2(x, y)
#define __AEArrayVar(x, line) __AEArrayVar1(x, line)

#define AEArrayEnumeratePointers(array, type, varname)                                        \
    AEArrayToken __AEArrayVar(token, __LINE__) = AEArrayGetToken(array);                      \
    int __AEArrayVar(count, __LINE__) = AEArrayGetCount(__AEArrayVar(token, __LINE__));       \
    int __AEArrayVar(i, __LINE__) = 0;                                                        \
    for (type varname = __AEArrayVar(count, __LINE__) > 0                                     \
                            ? (type)AEArrayGetItem(__AEArrayVar(token, __LINE__), 0)          \
                            : NULL;                                                           \
         __AEArrayVar(i, __LINE__) < __AEArrayVar(count, __LINE__);                           \
         __AEArrayVar(i, __LINE__)++,                                                         \
        varname = __AEArrayVar(i, __LINE__) < __AEArrayVar(count, __LINE__)                   \
                      ? (type)AEArrayGetItem(__AEArrayVar(token, __LINE__),                   \
                                             __AEArrayVar(i, __LINE__))                       \
                      : NULL)
