//
//  AkaiMidiMix.h
//  AudioKitSynthOne - Linux / Windows port
//

#pragma once

#include <memory>

#include "ControllerDriver.h"

namespace s1::ctrldev {

std::unique_ptr<ControllerDriver> makeAkaiMidiMix();

} // namespace s1::ctrldev
