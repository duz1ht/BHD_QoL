#include <cassert>
#include <cmath>

#include "visual_interpolation_math.h"

namespace {
visual_interpolation::Matrix4x4 Identity() {
    visual_interpolation::Matrix4x4 matrix = {};
    for (int i = 0; i < 4; ++i) matrix.m[i][i] = 1.0f;
    return matrix;
}
}

int main() {
    using namespace visual_interpolation;

    assert(AlphaFor250Hz(0) == 0);
    assert(AlphaFor250Hz(32) == 32768);
    assert(AlphaFor250Hz(64) == visual_camera::kInterpolationOne);
    assert(AlphaFor62Hz(1, 0) == 0);
    assert(AlphaFor62Hz(2, 0) == 16384);
    assert(AlphaFor62Hz(3, 0) == 32768);
    assert(AlphaFor62Hz(0, 0) == 49152);
    assert(AlphaFor62Hz(1, 63) == 16128);

    const TransformQ16 previous = {0, 100, -100, 0xfffffff0u, 0, 0};
    const TransformQ16 current = {100, 200, 100, 0x10u, 0x40000000u, 0};
    const TransformQ16 half = InterpolateTransform(previous, current, 32768);
    assert(half.x == 50);
    assert(half.y == 150);
    assert(half.z == 0);
    assert(half.yaw == 0);
    assert(half.pitch == 0x20000000u);

    Matrix4x4 transform = Identity();
    transform.m[0][0] = 0.0f;
    transform.m[0][1] = 1.0f;
    transform.m[1][0] = -1.0f;
    transform.m[1][1] = 0.0f;
    transform.m[3][0] = 10.0f;
    transform.m[3][1] = 4.0f;
    Matrix4x4 inverse = {};
    assert(InvertAffine(transform, &inverse));
    const Matrix4x4 product = Multiply(transform, inverse);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const float expected = row == column ? 1.0f : 0.0f;
            assert(std::fabs(product.m[row][column] - expected) < 0.0001f);
        }
    }

    Matrix4x4 currentRoot = Identity();
    currentRoot.m[3][0] = 10.0f;
    Matrix4x4 interpolatedRoot = Identity();
    interpolatedRoot.m[3][0] = 11.0f;
    assert(InvertAffine(currentRoot, &inverse));
    const Matrix4x4 correction = Multiply(inverse, interpolatedRoot);
    const Matrix4x4 corrected = Multiply(currentRoot, correction);
    assert(std::fabs(corrected.m[3][0] - 11.0f) < 0.0001f);

    assert(!IsTeleport(previous, current, 1000));
    TransformQ16 teleported = current;
    teleported.x = 10000;
    assert(IsTeleport(previous, teleported, 1000));
    return 0;
}
