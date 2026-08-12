//
//  NovationLaunchkeyMiniMk3.h
//  AudioKitSynthOne - Linux / Windows port
//

#pragma once

#include <memory>

#include "ControllerDriver.h"

namespace s1::ctrldev {

std::unique_ptr<ControllerDriver> makeNovationLaunchkeyMiniMk3();

} // namespace s1::ctrldev
