//
//  NovationLaunchkeyMk4.h
//  AudioKitSynthOne - Linux / Windows port
//

#pragma once

#include <memory>

#include "ControllerDriver.h"

namespace s1::ctrldev {

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMk4_37();

} // namespace s1::ctrldev
