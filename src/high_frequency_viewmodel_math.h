#pragma once

namespace high_frequency_viewmodel {

constexpr int kMatrixValueCount = 16;

void MultiplyMatrix(const float left[kMatrixValueCount],
                    const float right[kMatrixValueCount],
                    float output[kMatrixValueCount]);
void InvertRigidTransform(const float input[kMatrixValueCount],
                          float output[kMatrixValueCount]);
void InterpolateRigidTransform(const float previous[kMatrixValueCount],
                               const float current[kMatrixValueCount], float alpha,
                               float output[kMatrixValueCount]);
void ApplyPresentationCameraDelta(float worldMatrix[kMatrixValueCount],
                                  const float simulationCamera[kMatrixValueCount],
                                  const float presentationCamera[kMatrixValueCount]);

}  // namespace high_frequency_viewmodel
