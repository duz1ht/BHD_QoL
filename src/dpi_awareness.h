#pragma once

namespace dpi_awareness {

// Requests process-wide System DPI awareness. This must run before the game
// creates its first top-level window.
void Initialize(bool enabled);

}  // namespace dpi_awareness
