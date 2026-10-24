#pragma once

#include <windows.h>

namespace raw_input {

bool Install(bool enabled);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained();
void HandleDestroy();
bool GetProjectedNativeMovement(LONG* x, LONG* y);
unsigned long GetSimulationSerial();

}  // namespace raw_input
