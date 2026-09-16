#pragma once

#include <cstdint>
#include <limits>

namespace visual_mouse_late_latch_math {

struct CounterSample {
    int64_t capturedX = 0;
    int64_t capturedY = 0;
    int64_t consumedX = 0;
    int64_t consumedY = 0;
    uint64_t generation = 0;
    uint64_t captureSequence = 0;
    uint64_t consumedSequence = 0;
    bool valid = false;
};

struct PendingCounts {
    int64_t x = 0;
    int64_t y = 0;
    bool valid = false;
};

// Unsigned subtraction gives the defined two's-complement modular difference.  A
// reset is represented by a generation change and is never interpreted as motion.
inline int64_t ModularDifference(int64_t captured, int64_t consumed) {
    const uint64_t difference = static_cast<uint64_t>(captured) -
                                static_cast<uint64_t>(consumed);
    if (difference <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return static_cast<int64_t>(difference);
    return -1 - static_cast<int64_t>(~difference);
}

inline PendingCounts CalculatePending(const CounterSample& sample,
                                      uint64_t expectedGeneration) {
    if (!sample.valid || sample.generation != expectedGeneration)
        return {};
    return {ModularDifference(sample.capturedX, sample.consumedX),
            ModularDifference(sample.capturedY, sample.consumedY), true};
}

struct Q16Scaler {
    int32_t scale = 0;
    int64_t remainderX = 0;
    int64_t remainderY = 0;
    bool active = false;
};

inline int64_t ScaleQ16(int64_t counts, int32_t scale, int axis, Q16Scaler* state) {
    if (state == nullptr) return 0;
    if (!state->active || state->scale != scale) {
        state->scale = scale;
        state->remainderX = state->remainderY = 0;
        state->active = true;
    }
    int64_t& remainder = axis == 0 ? state->remainderX : state->remainderY;
    const int64_t value = counts * static_cast<int64_t>(scale) + remainder;
    const int64_t output = value / 65536;
    remainder = value - output * 65536;
    return output;
}

struct ConversionConfig {
    int32_t sensitivityQ16 = 0;
    int32_t adsScaleQ16 = 65536;
    int32_t scopeScaleQ16 = 65536;
    uint32_t yawUnitsPerCount = 0;
    int32_t pitchUnitsPerCount = 0;
    bool invertVertical = false;
    uint32_t cameraMode = 0;
};

inline uint64_t ConfigurationKey(const ConversionConfig& config) {
    uint64_t key = static_cast<uint32_t>(config.sensitivityQ16);
    key = key * 1099511628211ULL ^ static_cast<uint32_t>(config.adsScaleQ16);
    key = key * 1099511628211ULL ^ static_cast<uint32_t>(config.scopeScaleQ16);
    key = key * 1099511628211ULL ^ config.yawUnitsPerCount;
    key = key * 1099511628211ULL ^ static_cast<uint32_t>(config.pitchUnitsPerCount);
    key = key * 1099511628211ULL ^ (config.invertVertical ? 1ULL : 0ULL);
    return key * 1099511628211ULL ^ config.cameraMode;
}

inline bool NeedsRebase(uint64_t previousKey, bool hadConfiguration,
                        const ConversionConfig& current) {
    return hadConfiguration && previousKey != ConfigurationKey(current);
}

struct AngleDelta {
    uint32_t yaw = 0;
    int32_t pitch = 0;
};

inline AngleDelta ConvertToFullTurn(int64_t x, int64_t y,
                                    const ConversionConfig& config,
                                    Q16Scaler* scaler) {
    int64_t scaledX = ScaleQ16(x, config.sensitivityQ16, 0, scaler);
    int64_t scaledY = ScaleQ16(y, config.sensitivityQ16, 1, scaler);
    scaledX = scaledX * config.adsScaleQ16 / 65536;
    scaledY = scaledY * config.adsScaleQ16 / 65536;
    scaledX = scaledX * config.scopeScaleQ16 / 65536;
    scaledY = scaledY * config.scopeScaleQ16 / 65536;
    int64_t pitch = scaledY * config.pitchUnitsPerCount;
    if (config.invertVertical) pitch = -pitch;
    return {static_cast<uint32_t>(scaledX * config.yawUnitsPerCount),
            static_cast<int32_t>(pitch)};
}

inline int32_t ClampPitch(int32_t rootPitch, int32_t delta,
                          int32_t minimum, int32_t maximum) {
    int64_t result = static_cast<int64_t>(rootPitch) + delta;
    if (result < minimum) result = minimum;
    if (result > maximum) result = maximum;
    return static_cast<int32_t>(result - rootPitch);
}

struct ReconcileState {
    uint64_t generation = 0;
    uint64_t captureSequence = 0;
    uint64_t consumedSequence = 0;
    int64_t visualX = 0;
    int64_t visualY = 0;
    int64_t consumedX = 0;
    int64_t consumedY = 0;
    bool initialized = false;
};

struct ReconcileResult {
    int64_t pendingX = 0;
    int64_t pendingY = 0;
    int64_t reconciledX = 0;
    int64_t reconciledY = 0;
    bool rebase = false;
    bool valid = false;
};

inline ReconcileResult Reconcile(const CounterSample& sample, ReconcileState* state) {
    if (state == nullptr || !sample.valid) return {};
    const PendingCounts pending = CalculatePending(sample, sample.generation);
    if (!pending.valid) return {};
    if (!state->initialized || state->generation != sample.generation ||
        sample.captureSequence < state->captureSequence ||
        sample.consumedSequence < state->consumedSequence) {
        *state = {sample.generation, sample.captureSequence, sample.consumedSequence,
                  pending.x, pending.y, sample.consumedX, sample.consumedY, true};
        return {pending.x, pending.y, 0, 0, true, true};
    }
    const bool newlyConsumed = sample.consumedSequence != state->consumedSequence;
    const int64_t reconciledX = newlyConsumed
        ? ModularDifference(sample.consumedX, state->consumedX) : 0;
    const int64_t reconciledY = newlyConsumed
        ? ModularDifference(sample.consumedY, state->consumedY) : 0;
    state->captureSequence = sample.captureSequence;
    state->consumedSequence = sample.consumedSequence;
    state->visualX = pending.x;
    state->visualY = pending.y;
    state->consumedX = sample.consumedX;
    state->consumedY = sample.consumedY;
    return {pending.x, pending.y, reconciledX, reconciledY, false, true};
}

}  // namespace visual_mouse_late_latch_math
