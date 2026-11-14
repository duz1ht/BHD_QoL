#include "camera_position_math.h"

#include <limits>

namespace camera_position {
namespace {
bool Discontinuous(Position a, Position b) {
    constexpr int64_t limit = 0x400000;
    const int64_t delta[] = {int64_t(a.x) - b.x, int64_t(a.y) - b.y, int64_t(a.z) - b.z};
    for (int64_t d : delta) if (d > limit || d < -limit) return true;
    return false;
}
int32_t Offset(int32_t previous, int32_t current, uint32_t phase) {
    const int64_t visual = previous + ((int64_t(current) - previous) * phase) / 65536;
    return static_cast<int32_t>(visual - current);
}
bool Fits(int64_t value) {
    return value >= std::numeric_limits<int32_t>::min() &&
           value <= std::numeric_limits<int32_t>::max();
}
}  // namespace

bool operator==(Position a, Position b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool operator==(const Identity& a, const Identity& b) {
    return a.actor == b.actor && a.pool == b.pool && a.definitionIndex == b.definitionIndex &&
           a.definition == b.definition && a.logicalId == b.logicalId && a.mount == b.mount &&
           a.fastCallback == b.fastCallback && a.slowCallback == b.slowCallback;
}
uint32_t PhaseQ16(Cadence cadence, uint32_t remainder, uint32_t fastStep) {
    if (remainder >= 64) return 65536;
    if (cadence == Cadence::Fast) return remainder * 1024;
    return ((((fastStep - 1) & 3) * 64) + remainder) * 256;
}
bool ApplyOffset(Position camera, Position offset, Position* output) {
    const int64_t x = int64_t(camera.x) + offset.x;
    const int64_t y = int64_t(camera.y) + offset.y;
    const int64_t z = int64_t(camera.z) + offset.z;
    if (output == nullptr || !Fits(x) || !Fits(y) || !Fits(z)) return false;
    *output = {static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)};
    return true;
}
void PositionHistory::Reset() {
    if (currentValid_ || previousValid_) ++epoch_;
    currentValid_ = previousValid_ = false;
}
void PositionHistory::Observe(const Identity& identity, Position position,
                              uint32_t step, Cadence cadence) {
    if (!currentValid_ || !(identity == identity_) || cadence != cadence_ ||
        step - step_ > 1 || Discontinuous(position, current_)) {
        ++epoch_;
        previous_ = current_ = position;
        identity_ = identity;
        step_ = step;
        cadence_ = cadence;
        currentValid_ = true;
        previousValid_ = false;
        return;
    }
    if (step != step_) {
        previous_ = current_;
        previousValid_ = true;
    }
    current_ = position;
    step_ = step;
}
bool PositionHistory::GetOffset(const Identity& identity, Position live, uint32_t step,
                                uint32_t remainder, uint32_t fastStep, Position* output) {
    if (!currentValid_ || output == nullptr) return false;
    if (!(identity == identity_) || !(live == current_) || step != step_ || remainder >= 64) {
        Reset();
        return false;
    }
    if (!previousValid_) return false;
    const uint32_t phase = PhaseQ16(cadence_, remainder, fastStep);
    *output = {Offset(previous_.x, current_.x, phase), Offset(previous_.y, current_.y, phase),
               Offset(previous_.z, current_.z, phase)};
    return true;
}
}  // namespace camera_position
