//
//  KorgNanoKontrol2.h
//  AudioKitSynthOne - Linux / Windows port
//

#pragma once

#include <memory>

#include "ControllerDriver.h"

namespace s1::ctrldev {

std::unique_ptr<ControllerDriver> makeKorgNanoKontrol2();

} // namespace s1::ctrldev
