//
//  S1AudioUnit.h
//  AudioKitSynthOne - Linux port
//
//  Replaces the Objective-C S1AudioUnit. Keeps the render/main thread
//  communication structs byte-for-byte identical to the iOS original, swaps the
//  @protocol for a C++ observer interface, and swaps the selector-marshalling
//  message queue for a lock-free ring drained by the host.
//
//  This header shadows AudioKitSynthOne/DSP/Audio Unit/S1AudioUnit.h: the Linux
//  build puts linux/compat on the include path and leaves the Apple original
//  off it, so the unmodified kernel sources pick this up instead.
//

#pragma once

#include "AppleTypes.h"
#include "S1Parameter.h"

#define S1_MAX_POLYPHONY (6)
#define S1_NUM_MIDI_NOTES (128)

class AEMessageQueue;

// helper for midi/render thread communication: held+playing notes
typedef struct NoteNumber {
    int   noteNumber;
    int   transpose;
    int   velocity;
    float amp;
} NoteNumber;

// helper for render/main thread communication:
// DSP updates UI elements lfo1Rate, lfo2Rate, autoPanRate, delayTime when arpOn/tempoSyncArpRate update
typedef struct DependentParameter {
    S1Parameter parameter;
    float       normalizedValue; // [0,1] for ui
    float       value;
    int         payload;
} DependentParameter;

// helper for main+render thread communication: array of playing notes
typedef struct PlayingNotes {
    int        polyphony;
    NoteNumber playingNotes[S1_MAX_POLYPHONY];
} PlayingNotes;

// helper for main+render thread communication: array of held notes
typedef struct HeldNotes {
    int  heldNotesCount;
    bool heldNotes[S1_NUM_MIDI_NOTES];
} HeldNotes;

// helper for main+render thread communication: arp beat counter, and number of held notes
typedef struct S1ArpBeatCounter {
    int beatCounter;
    int heldNotesCount;
} S1ArpBeatCounter;

// ---------------------------------------------------------------------------
// S1Protocol: C++ stand-in for the Objective-C @protocol. Implemented by the
// host to observe DSP state changes.
// ---------------------------------------------------------------------------

class S1Protocol {
public:
    virtual ~S1Protocol() = default;

    virtual void dependentParameterDidChange(DependentParameter dependentParam) { (void)dependentParam; }
    virtual void arpBeatCounterDidChange(S1ArpBeatCounter arpBeatCounter) { (void)arpBeatCounter; }
    virtual void heldNotesDidChange(HeldNotes heldNotes) { (void)heldNotes; }
    virtual void playingNotesDidChange(PlayingNotes playingNotes) { (void)playingNotes; }
};

// ---------------------------------------------------------------------------
// S1AudioUnit: owns the render->control message queue and the delegate. The
// kernel reaches it as `audioUnit->_messageQueue`, matching the original.
// ---------------------------------------------------------------------------

class S1AudioUnit {
public:
    S1AudioUnit();
    ~S1AudioUnit();

    /// Called from the render thread by S1DSPKernel.
    void postDependentParameterDidChange(DependentParameter param);
    void postArpBeatCounterDidChange(S1ArpBeatCounter beatCounter);
    void postHeldNotesDidChange(HeldNotes heldNotes);
    void postPlayingNotesDidChange(PlayingNotes playingNotes);

    /// Called from the host's control thread; dispatches queued messages to the
    /// delegate. Returns the number of messages delivered.
    int drainMessageQueue();

    AEMessageQueue *_messageQueue = nullptr;
    S1Protocol     *s1Delegate    = nullptr;
};
