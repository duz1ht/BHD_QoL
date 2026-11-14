#pragma once

#include <cstdint>

namespace bhd_patches {
// Called by the proxy before forwarding WinMM calls. Only verified startup
// call sites in the supported game may initialize the patches, once.
void InitializeForWinmmCall(uintptr_t returnAddress);
}  // namespace bhd_patches
