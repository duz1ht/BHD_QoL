#pragma once

#include <cstdint>

namespace mouse_scaling_fix {

bool Install(bool enabled);
void Reset();
int32_t PreviewScale(int32_t delta, int32_t scale, int32_t axis);

} // namespace mouse_scaling_fix
