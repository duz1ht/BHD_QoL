#include "raw_input_math.h"

namespace raw_input {

int32_t ProjectNativeAxis(int32_t pendingRaw, int32_t previousTerm,
                          int32_t substeps) {
    if (substeps <= 0) substeps = 1;
    const int32_t currentTerm = static_cast<int32_t>(
        (static_cast<int64_t>(pendingRaw) * 4) / substeps);
    return previousTerm + currentTerm;
}

int32_t ProjectPresentationAxis(int32_t pendingRaw, int32_t previousTerm,
                                int32_t substeps, uint32_t phaseQ16) {
    if (substeps <= 0) substeps = 1;
    if (phaseQ16 > 0x10000U) phaseQ16 = 0x10000U;
    const int32_t currentTerm = static_cast<int32_t>(
        (static_cast<int64_t>(pendingRaw) * 4) / substeps);
    const int32_t interpolatedTail = static_cast<int32_t>(
        (static_cast<int64_t>(previousTerm) * phaseQ16) / 0x10000);
    return interpolatedTail + currentTerm;
}

}  // namespace raw_input
