#pragma once

#include <windows.h>

namespace d3d8_resolution_reset {

bool Initialize(bool enabled);
void Request(LONG width, LONG height);
void SetGameActive(bool active);

}  // namespace d3d8_resolution_reset
