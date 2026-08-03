//
//  S1DSPKernel+didChanges.cpp
//  AudioKitSynthOne - Linux port
//
//  Replaces AudioKitSynthOne/DSP/Kernel/S1DSPKernel+didChanges.mm. Upstream
//  marshals each notification to the main thread as an Objective-C selector
//  plus a struct argument; here the same structs go onto S1AudioUnit's
//  lock-free ring, which the host drains on its control thread.
//
//  These are all callable from the render loop, so nothing here allocates,
//  locks, or blocks.
//

#include "S1DSPKernel.hpp"
#include "S1NoteState.hpp"
#include "AEArray.h"
#include "AEMessageQueue.h"

void S1DSPKernel::dependentParameterDidChange(DependentParameter param) {
    if (audioUnit == nullptr) return;
    audioUnit->postDependentParameterDidChange(param);
}

//can be called from within the render loop
void S1DSPKernel::beatCounterDidChange() {
    S1ArpBeatCounter retVal = {sequencer.getArpBeatCount(), heldNoteNumbersAE.count};
    if (audioUnit == nullptr) return;
    audioUnit->postArpBeatCounterDidChange(retVal);
}

///can be called from within the render loop
void S1DSPKernel::playingNotesDidChange() {
    aePlayingNotes.polyphony = S1_MAX_POLYPHONY;
    if (parameters[isMono] > 0.f) {
        aePlayingNotes.playingNotes[0] = {monoNote->rootNoteNumber, monoNote->transpose,
                                          monoNote->velocity, monoNote->amp};
        for (int i = 1; i < S1_MAX_POLYPHONY; i++) {
            aePlayingNotes.playingNotes[i] = {-1, -1, -1, -1};
        }
    } else {
        for (int i = 0; i < S1_MAX_POLYPHONY; i++) {
            const auto &note = (*noteStates)[i];
            aePlayingNotes.playingNotes[i] = {note.rootNoteNumber, note.transpose,
                                              note.velocity, note.amp};
        }
    }
    if (audioUnit == nullptr) return;
    audioUnit->postPlayingNotesDidChange(aePlayingNotes);
}

///can be called from within the render loop
void S1DSPKernel::heldNotesDidChange() {
    for (int i = 0; i < S1_NUM_MIDI_NOTES; i++)
        aeHeldNotes.heldNotes[i] = false;
    int count = 0;
    AEArrayEnumeratePointers(heldNoteNumbersAE, NoteNumber *, note) {
        const int nn = note->noteNumber;
        aeHeldNotes.heldNotes[nn] = true;
        ++count;
    }
    aeHeldNotes.heldNotesCount = count;
    if (audioUnit == nullptr) return;
    audioUnit->postHeldNotesDidChange(aeHeldNotes);
}
