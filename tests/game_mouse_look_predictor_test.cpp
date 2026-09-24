#include "game_mouse_look_predictor.h"

#include <cassert>

using namespace game_mouse_look_predictor;

int main() {
    assert(PredictYawControl(0, 0) == 0);
    assert(PredictYawControl(8, 0) == 7 * 3072);
    assert(PredictYawControl(-8, 0) == -7 * 3072);
    assert(PredictYawControl(1, 7) == 7 * 3072);

    assert(PredictPitchControl(0, true) == 0);
    assert(PredictPitchControl(4, false) == 65536);
    assert(PredictPitchControl(4, true) == 135168);
    assert(PredictPitchControl(-4, true) == -135168);

    assert(ControlToStep(0) == 0);
    assert(ControlToStep(2) == 1);
    assert(ControlToStep(-2) == 0);
    assert(ControlToStep(-3) == -1);
    assert(ControlToStep(-6) == -1);
    return 0;
}
