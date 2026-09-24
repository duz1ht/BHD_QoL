#pragma once

namespace presentation_state {

struct Settings {
    bool enabled;
    bool cameraPresentation;
    bool renderRateMouse;
};

bool Install(const Settings& settings);

}  // namespace presentation_state
