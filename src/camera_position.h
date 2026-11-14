#pragma once

#include <cstdint>

namespace camera_position {
bool Install(bool enabled);
bool IsEnabled();
// Applies XYZ only to a private camera description. Never writes actor memory.
bool Apply(void* camera);
uint32_t GetSimulationSerial();
uint32_t GetHistoryEpoch();
}  // namespace camera_position
