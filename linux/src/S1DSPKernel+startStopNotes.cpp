//
//  S1DSPKernel+startStopNotes.cpp
//  AudioKitSynthOne - Linux port
//
//  Replaces AudioKitSynthOne/DSP/Kernel/S1DSPKernel+startStopNotes.mm, whose
//  held-note bookkeeping is written against NSMutableArray/NSValue. The logic
//  below is a direct transcription; only the container changes.
//

#include "S1DSPKernel.hpp"
#include "AEArray.h"

// NOTE ON
// startNote is not called by render thread, but turnOnKey is
void S1DSPKernel::startNote(int noteNumber, int velocity) {
    if (noteNumber < 0 || noteNumber >= S1_NUM_MIDI_NOTES)
        return;

    startNote(noteNumber, velocity, 1.f); // freq unused
}

// NOTE ON
// startNote is not called by render thread, but turnOnKey is
void S1DSPKernel::startNote(int noteNumber, int velocity, float frequency) {
    (void)frequency;
    if (noteNumber < 0 || noteNumber >= S1_NUM_MIDI_NOTES)
        return;

    for (size_t i = 0; i < heldNoteNumbers.size(); i++) {
        if (heldNoteNumbers[i].noteNumber == noteNumber) {
            heldNoteNumbers.erase(heldNoteNumbers.begin() + static_cast<long>(i));
            break;
        }
    }

    NoteNumber note = {noteNumber, (int)parameters[transpose], velocity, 0.f};
    heldNoteNumbers.insert(heldNoteNumbers.begin(), note);
    heldNoteNumbersAE.update(heldNoteNumbers);

    // the tranpose feature leads to the override the AKPolyphonicNode::startNote frequency
    const float frequencyTranposeOverride = tuningTableNoteToHz(noteNumber + (int)parameters[transpose]);

    // ARP/SEQ
    if (parameters[arpIsOn] == 1.f) {
        return;
    } else {
        turnOnKey(noteNumber, velocity, frequencyTranposeOverride);
    }
}

// NOTE OFF...put into release mode
void S1DSPKernel::stopNote(int noteNumber) {
    if (noteNumber < 0 || noteNumber >= S1_NUM_MIDI_NOTES)
        return;

    long index = -1;
    for (size_t i = 0; i < heldNoteNumbers.size(); i++) {
        if (heldNoteNumbers[i].noteNumber == noteNumber) {
            index = static_cast<long>(i);
            break;
        }
    }
    if (index != -1)
        heldNoteNumbers.erase(heldNoteNumbers.begin() + index);

    heldNoteNumbersAE.update(heldNoteNumbers);

    // ARP/SEQ
    if (parameters[arpIsOn] == 1.f)
        return;
    else
        turnOffKey(noteNumber);
}

///puts all notes in release mode...no artifacts
void S1DSPKernel::stopAllNotes() {
    heldNoteNumbers.clear();
    heldNoteNumbersAE.update(heldNoteNumbers);
    if (parameters[isMono] > 0.f) {
        stopNote(60);
    } else {
        for (int i = 0; i < S1_NUM_MIDI_NOTES; i++)
            stopNote(i);
    }
}
