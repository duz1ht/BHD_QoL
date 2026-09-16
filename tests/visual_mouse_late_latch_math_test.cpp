#include "visual_mouse_late_latch_math.h"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace visual_mouse_late_latch_math;

int main() {
    // Partial authoritative consumption and a report arriving after that poll.
    CounterSample sample{12, -9, 5, -4, 7, 3, 1, true};
    PendingCounts pending = CalculatePending(sample, 7);
    assert(pending.valid && pending.x == 7 && pending.y == -5);
    sample.capturedX += 4;
    ++sample.captureSequence;
    pending = CalculatePending(sample, 7);
    assert(pending.x == 11); // the renderer did not drain authoritative state

    // Generation/reset and sequence wrap are rebases rather than old-session input.
    assert(!CalculatePending(sample, 8).valid);
    ReconcileState reconcile;
    auto first = Reconcile(sample, &reconcile);
    assert(first.valid && first.rebase && first.pendingX == 11);
    sample.generation = 8;
    sample.captureSequence = 0;
    sample.consumedSequence = 0;
    sample.capturedX = sample.consumedX = 0;
    auto reset = Reconcile(sample, &reconcile);
    assert(reset.valid && reset.rebase && reset.pendingX == 0);
    assert(ModularDifference(std::numeric_limits<int64_t>::min(),
                             std::numeric_limits<int64_t>::max()) == 1);

    // Q16 fractions are independent per axis and survive multiple visual samples.
    Q16Scaler scaler;
    assert(ScaleQ16(1, 32768, 0, &scaler) == 0);
    assert(ScaleQ16(1, 32768, 0, &scaler) == 1);
    assert(ScaleQ16(1, 32768, 1, &scaler) == 0);
    assert(scaler.remainderY == 32768);

    ConversionConfig config;
    config.sensitivityQ16 = 65536;
    config.yawUnitsPerCount = 100;
    config.pitchUnitsPerCount = 50;
    config.invertVertical = true;
    scaler = {};
    AngleDelta angles = ConvertToFullTurn(2, 3, config, &scaler);
    assert(angles.yaw == 200 && angles.pitch == -150);
    assert(ClampPitch(90, 20, -100, 100) == 10);
    assert(ClampPitch(-90, -20, -100, 100) == -10);

    // ADS/scope, sensitivity, inversion, and camera-mode changes force rebasing.
    const uint64_t originalKey = ConfigurationKey(config);
    assert(!NeedsRebase(originalKey, false, config));
    config.adsScaleQ16 = 32768;
    assert(NeedsRebase(originalKey, true, config));
    const uint64_t adsKey = ConfigurationKey(config);
    config.scopeScaleQ16 = 16384;
    assert(NeedsRebase(adsKey, true, config));
    config.cameraMode = 1;
    assert(NeedsRebase(ConfigurationKey(ConversionConfig{}), true, config));

    // Consumption is reconciled once by producer sequence, even for equal values.
    sample = {10, 4, 0, 0, 1, 5, 1, true};
    reconcile = {};
    first = Reconcile(sample, &reconcile);
    assert(first.pendingX == 10);
    sample.consumedX = 6;
    sample.consumedY = 4;
    ++sample.consumedSequence;
    auto consumed = Reconcile(sample, &reconcile);
    assert(consumed.pendingX == 4 && consumed.pendingY == 0);
    assert(consumed.reconciledX == 6 && consumed.reconciledY == 4);
    auto sameFrame = Reconcile(sample, &reconcile);
    assert(sameFrame.reconciledX == 0 && sameFrame.reconciledY == 0);

    // When the authoritative root incorporates the poll, removing the matching
    // visual offset yields the same displayed total and therefore no jump.
    const int64_t oldRoot = 100;
    const int64_t oldVisual = 10;
    const int64_t newRoot = 106;
    assert(oldRoot + oldVisual == newRoot + consumed.pendingX);

    // A read-only query model: calculating pending never mutates source counters.
    const CounterSample before = sample;
    (void)CalculatePending(sample, sample.generation);
    assert(sample.capturedX == before.capturedX && sample.consumedX == before.consumedX);

    // Focus loss/recovery is represented by invalidity followed by generation rebase.
    sample.valid = false;
    assert(!Reconcile(sample, &reconcile).valid);
    sample.valid = true;
    ++sample.generation;
    assert(Reconcile(sample, &reconcile).rebase);
    return 0;
}
