#include "high_frequency_viewmodel_math.h"

#include <cassert>

int main() {
    const float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float matrix[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 4,5,6,1 };
    high_frequency_viewmodel::ApplyPresentationCameraDelta(matrix, identity, identity);
    assert(matrix[12] == 4 && matrix[13] == 5 && matrix[14] == 6);
    float presentation[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 10,0,0,1 };
    high_frequency_viewmodel::ApplyPresentationCameraDelta(matrix, identity, presentation);
    assert(matrix[12] == 14 && matrix[13] == 5 && matrix[14] == 6);

    float previous[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float current[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 8,4,2,1 };
    float interpolated[16] = {};
    high_frequency_viewmodel::InterpolateRigidTransform(previous, current, 0.5f,
                                                        interpolated);
    assert(interpolated[12] == 4 && interpolated[13] == 2 && interpolated[14] == 1);

    float inverse[16] = {};
    float product[16] = {};
    high_frequency_viewmodel::InvertRigidTransform(current, inverse);
    high_frequency_viewmodel::MultiplyMatrix(inverse, current, product);
    assert(product[0] == 1 && product[5] == 1 && product[10] == 1 && product[15] == 1);
    assert(product[12] == 0 && product[13] == 0 && product[14] == 0);
    return 0;
}
