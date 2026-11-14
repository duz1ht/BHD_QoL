#pragma once

namespace allow_high_resolution_fonts {
// Enables per-file physical FNT0 atlas dimensions. Does not synthesize glyphs,
// modify original assets, or change text scale/aspect/spacing.
bool Install(bool enabled);
}  // namespace allow_high_resolution_fonts
