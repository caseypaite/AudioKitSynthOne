//
//  BufferedAudioBus.hpp
//  AudioKitSynthOne - Linux port
//
//  Upstream this manages AVAudioPCMBuffer-backed AudioUnit busses. The Linux
//  host owns its own buffers and hands the kernel an AudioBufferList directly
//  via AKOutputBuffered::setBuffer, so nothing is needed here.
//

#pragma once

#include "AKSoundpipeKernel.hpp"
