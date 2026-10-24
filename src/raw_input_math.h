#pragma once

#include <cstdint>

namespace raw_input {

int32_t ProjectNativeAxis(int32_t pendingRaw, int32_t previousTerm,
                          int32_t substeps);
int32_t ProjectPresentationAxis(int32_t pendingRaw, int32_t previousTerm,
                                int32_t substeps, uint32_t phaseQ16);

}  // namespace raw_input
