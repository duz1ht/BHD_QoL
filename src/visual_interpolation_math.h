#pragma once

#include <cmath>
#include <cstdint>

#include "visual_camera_math.h"

namespace visual_interpolation {

inline std::uint32_t AlphaFor250Hz(std::uint32_t remainder) {
    if (remainder >= 64) return visual_camera::kInterpolationOne;
    return remainder * visual_camera::kInterpolationOne / 64;
}

inline std::uint32_t AlphaFor62Hz(std::uint32_t postIncrementPhase,
                                  std::uint32_t remainder) {
    if (remainder >= 64) return visual_camera::kInterpolationOne;
    // The 62.5 Hz callback runs when the pre-increment phase is zero, so its
    // freshly captured snapshot corresponds to post-increment phase one.
    const std::uint32_t completedQuarters = (postIncrementPhase - 1u) & 3u;
    const std::uint32_t units = completedQuarters * 64u + remainder;
    return units * visual_camera::kInterpolationOne / 256u;
}

struct TransformQ16 {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::uint32_t yaw;
    std::uint32_t pitch;
    std::uint32_t roll;
};

struct Matrix4x4 {
    float m[4][4];
};

static_assert(sizeof(TransformQ16) == 0x18, "Unexpected DFBHD transform layout");
static_assert(sizeof(Matrix4x4) == 0x40, "Unexpected DFBHD matrix layout");

inline TransformQ16 InterpolateTransform(const TransformQ16& previous,
                                         const TransformQ16& current,
                                         std::uint32_t alphaQ16) {
    return {
        visual_camera::InterpolateLinear(previous.x, current.x, alphaQ16),
        visual_camera::InterpolateLinear(previous.y, current.y, alphaQ16),
        visual_camera::InterpolateLinear(previous.z, current.z, alphaQ16),
        visual_camera::InterpolateAngle(previous.yaw, current.yaw, alphaQ16),
        visual_camera::InterpolateAngle(previous.pitch, current.pitch, alphaQ16),
        visual_camera::InterpolateAngle(previous.roll, current.roll, alphaQ16),
    };
}

inline Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 result = {};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                result.m[row][column] += a.m[row][index] * b.m[index][column];
            }
        }
    }
    return result;
}

// DFBHD submits affine row-vector matrices with translation in the fourth row.
// This inverse deliberately rejects non-affine or singular matrices so callers
// can render the native transform rather than risk a malformed visual copy.
inline bool InvertAffine(const Matrix4x4& input, Matrix4x4* output) {
    if (output == nullptr || std::fabs(input.m[0][3]) > 0.0001f ||
        std::fabs(input.m[1][3]) > 0.0001f ||
        std::fabs(input.m[2][3]) > 0.0001f ||
        std::fabs(input.m[3][3] - 1.0f) > 0.0001f) {
        return false;
    }

    const float a = input.m[0][0], b = input.m[0][1], c = input.m[0][2];
    const float d = input.m[1][0], e = input.m[1][1], f = input.m[1][2];
    const float g = input.m[2][0], h = input.m[2][1], i = input.m[2][2];
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) +
                              c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::fabs(determinant) < 0.000001f) {
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;
    Matrix4x4 result = {};
    result.m[0][0] = (e * i - f * h) * inverseDeterminant;
    result.m[0][1] = (c * h - b * i) * inverseDeterminant;
    result.m[0][2] = (b * f - c * e) * inverseDeterminant;
    result.m[1][0] = (f * g - d * i) * inverseDeterminant;
    result.m[1][1] = (a * i - c * g) * inverseDeterminant;
    result.m[1][2] = (c * d - a * f) * inverseDeterminant;
    result.m[2][0] = (d * h - e * g) * inverseDeterminant;
    result.m[2][1] = (b * g - a * h) * inverseDeterminant;
    result.m[2][2] = (a * e - b * d) * inverseDeterminant;
    result.m[3][3] = 1.0f;

    const float tx = input.m[3][0];
    const float ty = input.m[3][1];
    const float tz = input.m[3][2];
    result.m[3][0] = -(tx * result.m[0][0] + ty * result.m[1][0] +
                       tz * result.m[2][0]);
    result.m[3][1] = -(tx * result.m[0][1] + ty * result.m[1][1] +
                       tz * result.m[2][1]);
    result.m[3][2] = -(tx * result.m[0][2] + ty * result.m[1][2] +
                       tz * result.m[2][2]);

    for (const auto& row : result.m) {
        for (float value : row) {
            if (!std::isfinite(value)) return false;
        }
    }
    *output = result;
    return true;
}

inline bool SameTransform(const TransformQ16& a, const TransformQ16& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.yaw == b.yaw &&
           a.pitch == b.pitch && a.roll == b.roll;
}

inline bool IsTeleport(const TransformQ16& a, const TransformQ16& b,
                       std::int32_t thresholdQ16) {
    const auto exceeds = [thresholdQ16](std::int32_t from, std::int32_t to) {
        const std::int64_t delta = static_cast<std::int64_t>(to) - from;
        return delta > thresholdQ16 || delta < -static_cast<std::int64_t>(thresholdQ16);
    };
    return exceeds(a.x, b.x) || exceeds(a.y, b.y) || exceeds(a.z, b.z);
}

}  // namespace visual_interpolation
