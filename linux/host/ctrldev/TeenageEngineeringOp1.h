//
//  TeenageEngineeringOp1.h
//  AudioKitSynthOne - Linux / Windows port
//

#pragma once

#include <memory>

#include "ControllerDriver.h"

namespace s1::ctrldev {

std::unique_ptr<ControllerDriver> makeTeenageEngineeringOp1();

} // namespace s1::ctrldev
