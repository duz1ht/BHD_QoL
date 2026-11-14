#include "camera_position_math.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {
void Check(bool valid) { if (!valid) { std::fputs("Camera position regression\n", stderr); std::abort(); } }
}
int main() {
    using namespace camera_position;
    Check(PhaseQ16(Cadence::Fast, 0, 1) == 0);
    Check(PhaseQ16(Cadence::Fast, 32, 1) == 32768);
    Check(PhaseQ16(Cadence::Fast, 63, 1) == 64512);
    Check(PhaseQ16(Cadence::Fast, 64, 1) == 65536);
    Check(PhaseQ16(Cadence::Slow, 0, 1) == 0);
    Check(PhaseQ16(Cadence::Slow, 0, 2) == 16384);
    Check(PhaseQ16(Cadence::Slow, 0, 3) == 32768);
    Check(PhaseQ16(Cadence::Slow, 0, 4) == 49152);
    Check(PhaseQ16(Cadence::Slow, 32, 2) == 24576);
    Check(PhaseQ16(Cadence::Slow, 0, 0) == 49152);

    Identity key = {0x1234, 0x1000, 1, 2, 3, 0, 4, 5};
    PositionHistory history;
    Position offset, camera;
    history.Observe(key, {100, 200, 300}, 10, Cadence::Fast);
    Check(!history.GetOffset(key, {100, 200, 300}, 10, 32, 10, &offset));
    history.Observe(key, {300, 400, 100}, 11, Cadence::Fast);
    Check(history.GetOffset(key, {300, 400, 100}, 11, 32, 11, &offset));
    Check(offset == Position{-100, -100, 100});
    Check(ApplyOffset({1300, 2400, 3100}, offset, &camera));
    Check(camera == Position{1200, 2300, 3200}); // preserve native eye/head offset
    Check(history.GetOffset(key, {300, 400, 100}, 11, 0, 11, &offset));
    Check(offset == Position{-200, -200, 200});

    // A duplicate capture updates current, never shifts previous twice.
    history.Observe(key, {500, 600, 100}, 11, Cadence::Fast);
    Check(history.GetOffset(key, {500, 600, 100}, 11, 32, 11, &offset));
    Check(offset == Position{-200, -200, 100});
    const uint32_t epoch = history.Epoch();
    Check(!history.GetOffset(key, {501, 600, 100}, 11, 32, 11, &offset));
    Check(history.Epoch() != epoch); // unobserved motion invalidates weapon history too

    history.Observe(key, {1000, 0, 0}, 20, Cadence::Slow);
    history.Observe(key, {2000, 0, 0}, 21, Cadence::Slow);
    Check(history.GetOffset(key, {2000, 0, 0}, 21, 32, 2, &offset));
    Check(offset == Position{-625, 0, 0});
    Identity other = key;
    other.logicalId++;
    Check(!history.GetOffset(other, {2000, 0, 0}, 21, 32, 2, &offset));

    // New target, mount/definition/pool/callback, cadence, skipped or rewound
    // producer steps must never interpolate across unrelated states.
    const auto freshPair = [&] {
        history.Reset();
        history.Observe(key, {0, 0, 0}, 1, Cadence::Fast);
        history.Observe(key, {100, 0, 0}, 2, Cadence::Fast);
    };
    for (int field = 0; field < 8; ++field) {
        freshPair(); other = key;
        switch (field) {
        case 0: ++other.actor; break; case 1: ++other.pool; break;
        case 2: ++other.definitionIndex; break; case 3: ++other.definition; break;
        case 4: ++other.logicalId; break; case 5: ++other.mount; break;
        case 6: ++other.fastCallback; break; case 7: ++other.slowCallback; break;
        }
        history.Observe(other, {200, 0, 0}, 3, Cadence::Fast);
        Check(!history.GetOffset(other, {200, 0, 0}, 3, 32, 3, &offset));
    }
    for (uint32_t step : {1u, 4u, 100u}) {
        freshPair(); history.Observe(key, {200, 0, 0}, step, Cadence::Fast);
        Check(!history.GetOffset(key, {200, 0, 0}, step, 32, step, &offset));
    }
    freshPair(); history.Observe(key, {200, 0, 0}, 3, Cadence::Slow);
    Check(!history.GetOffset(key, {200, 0, 0}, 3, 32, 3, &offset));
    freshPair(); history.Observe(key, {0x400065, 0, 0}, 3, Cadence::Fast);
    Check(!history.GetOffset(key, {0x400065, 0, 0}, 3, 32, 3, &offset));
    freshPair(); Check(!history.GetOffset(key, {100, 0, 0}, 3, 32, 3, &offset));
    freshPair(); Check(!history.GetOffset(key, {100, 0, 0}, 2, 64, 2, &offset));
    history.Reset();
    history.Observe(key, {0, 0, 0}, 0xFFFFFFFFu, Cadence::Fast);
    history.Observe(key, {100, 0, 0}, 0, Cadence::Fast);
    Check(history.GetOffset(key, {100, 0, 0}, 0, 32, 0, &offset));
    Check(offset.x == -50); // natural counter wrap is continuous

    camera = {7, 8, 9};
    Check(!ApplyOffset({std::numeric_limits<int32_t>::max(), 0, 0}, {1, 2, 3}, &camera));
    Check(camera == Position{7, 8, 9}); // no partial writes on overflow
    Check(!ApplyOffset({0, std::numeric_limits<int32_t>::min(), 0}, {0, -1, 0}, &camera));
    Check(ApplyOffset({-100, 0, 0}, {-50, 0, 0}, &camera) && camera.x == -150);
    return 0;
}
