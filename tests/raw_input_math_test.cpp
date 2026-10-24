#include "raw_input_math.h"

#include <cassert>

int main() {
    using raw_input::ProjectNativeAxis;
    using raw_input::ProjectPresentationAxis;

    assert(ProjectNativeAxis(3, 8, 1) == 20);
    assert(ProjectNativeAxis(-3, 8, 1) == -4);
    assert(ProjectNativeAxis(0, 8, 1) == 8);
    assert(ProjectNativeAxis(5, 4, 2) == 14);
    assert(ProjectNativeAxis(-5, 4, 2) == -6);
    assert(ProjectNativeAxis(3, 8, 0) == 20);

    assert(ProjectPresentationAxis(0, 16, 1, 0) == 0);
    assert(ProjectPresentationAxis(0, 16, 1, 0x8000) == 8);
    assert(ProjectPresentationAxis(0, 16, 1, 0x10000) == 16);
    assert(ProjectPresentationAxis(3, 16, 1, 0) == 12);
    assert(ProjectPresentationAxis(3, 16, 1, 0x8000) == 20);
    assert(ProjectPresentationAxis(-3, -16, 1, 0x8000) == -20);
    assert(ProjectPresentationAxis(3, 16, 1, 0x20000) == 28);
    return 0;
}
