#pragma once

namespace cursor_visibility {

// Balances only the ShowCursor calls made here, leaving the game's normal menu
// cursor policy intact when focus returns.
void HandleGameDeactivated(const char* trigger);
void HandleGameActivated(const char* trigger);
void Shutdown();

}  // namespace cursor_visibility
