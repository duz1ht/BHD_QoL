#include "game_mouse_prediction.h"

#include <cassert>

int main() {
    using game_mouse_prediction::Simulate;
    const game_mouse_prediction::State state = {4, 0, 0, 65536, true, 1, 0};
    const auto zero = Simulate(state, 0, 0);
    assert(zero.yawControl == 0);
    assert(zero.pitchControl == 0);

    const auto moved = Simulate(state, 8, -4);
    assert(moved.yawControl == 21504);
    assert(moved.pitchControl == -135168);

    const game_mouse_prediction::State filtered = {4, 0, 0, 65536, true, 1, 16};
    assert(Simulate(filtered, 0, 0).yawControl == 43008);
    assert(Simulate(filtered, 8, 0).yawControl -
           Simulate(filtered, 0, 0).yawControl == 21504);

    const game_mouse_prediction::State zoomed = {4, 0, 0, 65536, false, 2, 0};
    assert(Simulate(zoomed, 8, 4).yawControl == 9216);
    assert(Simulate(zoomed, 0, 4).pitchControl == -67584);
}
