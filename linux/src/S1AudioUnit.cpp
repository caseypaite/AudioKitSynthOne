//
//  S1AudioUnit.cpp
//  AudioKitSynthOne - Linux port
//

#include "S1AudioUnit.h"
#include "AEMessageQueue.h"

S1AudioUnit::S1AudioUnit() : _messageQueue(new AEMessageQueue()) {}

S1AudioUnit::~S1AudioUnit() {
    delete _messageQueue;
    _messageQueue = nullptr;
}

void S1AudioUnit::postDependentParameterDidChange(DependentParameter param) {
    if (_messageQueue == nullptr) return;
    S1Message m;
    m.kind = S1Message::kDependentParameterDidChange;
    m.dependentParameter = param;
    _messageQueue->push(m);
}

void S1AudioUnit::postArpBeatCounterDidChange(S1ArpBeatCounter beatCounter) {
    if (_messageQueue == nullptr) return;
    S1Message m;
    m.kind = S1Message::kArpBeatCounterDidChange;
    m.arpBeatCounter = beatCounter;
    _messageQueue->push(m);
}

void S1AudioUnit::postHeldNotesDidChange(HeldNotes heldNotes) {
    if (_messageQueue == nullptr) return;
    S1Message m;
    m.kind = S1Message::kHeldNotesDidChange;
    m.heldNotes = heldNotes;
    _messageQueue->push(m);
}

void S1AudioUnit::postPlayingNotesDidChange(PlayingNotes playingNotes) {
    if (_messageQueue == nullptr) return;
    S1Message m;
    m.kind = S1Message::kPlayingNotesDidChange;
    m.playingNotes = playingNotes;
    _messageQueue->push(m);
}

int S1AudioUnit::drainMessageQueue() {
    if (_messageQueue == nullptr) return 0;

    int delivered = 0;
    S1Message m;
    while (_messageQueue->pop(m)) {
        ++delivered;
        if (s1Delegate == nullptr) continue;

        switch (m.kind) {
            case S1Message::kDependentParameterDidChange:
                s1Delegate->dependentParameterDidChange(m.dependentParameter);
                break;
            case S1Message::kArpBeatCounterDidChange:
                s1Delegate->arpBeatCounterDidChange(m.arpBeatCounter);
                break;
            case S1Message::kHeldNotesDidChange:
                s1Delegate->heldNotesDidChange(m.heldNotes);
                break;
            case S1Message::kPlayingNotesDidChange:
                s1Delegate->playingNotesDidChange(m.playingNotes);
                break;
            case S1Message::kNone:
                break;
        }
    }
    return delivered;
}
