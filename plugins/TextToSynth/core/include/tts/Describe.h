#pragma once

#include <string>

#include "serum2/PresetFile.h"
#include "tts/Targets.h"

namespace tts
{
// A readable snapshot of what shapes the sound: oscillators, filters,
// envelopes, LFO routes, FX chain with mix levels, macros and voicing. Uses
// the same friendly names the edit tools accept, so Claude can go straight
// from "too harsh" to the parameter that makes it so.
std::string describePreset(const serum2::Preset& preset, const TargetResolver& resolver);
} // namespace tts
