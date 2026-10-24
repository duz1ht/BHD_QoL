#pragma once

#include <cstdint>

namespace mouse_scaling_fix {

struct TraceSnapshot {
  unsigned long serial;
  int32_t inputX;
  int32_t inputY;
  int32_t outputX;
  int32_t outputY;
  int32_t scale;
};

bool Install(bool enabled);
void Reset();
int32_t PreviewScale(int32_t delta, int32_t scale, int32_t axis);
void GetTraceSnapshot(TraceSnapshot* snapshot);

} // namespace mouse_scaling_fix
