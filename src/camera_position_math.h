#pragma once

#include <cstdint>

namespace camera_position {
struct Position { int32_t x = 0, y = 0, z = 0; };
bool operator==(Position a, Position b);
enum class Cadence { Fast, Slow };

struct Identity {
    uintptr_t actor = 0, pool = 0;
    uint32_t definitionIndex = 0, definition = 0, logicalId = 0, mount = 0;
    uint32_t fastCallback = 0, slowCallback = 0;
};
bool operator==(const Identity& a, const Identity& b);

uint32_t PhaseQ16(Cadence cadence, uint32_t remainder, uint32_t fastStep);
bool ApplyOffset(Position camera, Position offset, Position* output);

class PositionHistory {
public:
    void Reset();
    void Observe(const Identity& identity, Position position, uint32_t step, Cadence cadence);
    bool GetOffset(const Identity& identity, Position live, uint32_t step,
                   uint32_t remainder, uint32_t fastStep, Position* output);
    uint32_t Epoch() const { return epoch_; }
    Cadence Producer() const { return cadence_; }
private:
    Identity identity_ = {};
    Position previous_ = {}, current_ = {};
    uint32_t step_ = 0, epoch_ = 0;
    Cadence cadence_ = Cadence::Fast;
    bool currentValid_ = false, previousValid_ = false;
};
}  // namespace camera_position
