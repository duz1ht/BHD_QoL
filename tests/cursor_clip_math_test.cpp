#include "cursor_clip_math.h"

#include <cassert>

int main() {
    using cursor_clip::CoversFullClient;
    using cursor_clip::RectEdges;

    const RectEdges fullHdClient = {0, 0, 1920, 1080};
    assert(CoversFullClient(fullHdClient, fullHdClient));
    assert(CoversFullClient({1, -1, 1919, 1081}, fullHdClient));
    assert(!CoversFullClient({0, 0, 1440, 1080}, fullHdClient));

    const RectEdges offsetClient = {-1920, 120, 0, 1200};
    assert(CoversFullClient(offsetClient, offsetClient));
    assert(CoversFullClient({-1919, 119, -1, 1201}, offsetClient));
    assert(!CoversFullClient({-1920, 120, -480, 1200}, offsetClient));

    assert(!CoversFullClient({0, 0, 0, 1080}, fullHdClient));
    assert(!CoversFullClient(fullHdClient, fullHdClient, -1));
    return 0;
}
