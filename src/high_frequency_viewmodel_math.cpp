#include "high_frequency_viewmodel_math.h"

#include <algorithm>
#include <cmath>

namespace high_frequency_viewmodel {
namespace {
struct Quaternion { float x, y, z, w; };

Quaternion Normalize(Quaternion q) {
    const float length = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (length <= 0.000001f) return {0, 0, 0, 1};
    return {q.x/length, q.y/length, q.z/length, q.w/length};
}

Quaternion MatrixToQuaternion(const float m[16]) {
    Quaternion q = {};
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m[6]-m[9])/s, (m[8]-m[2])/s, (m[1]-m[4])/s, 0.25f*s};
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q = {0.25f*s, (m[4]+m[1])/s, (m[8]+m[2])/s, (m[6]-m[9])/s};
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q = {(m[4]+m[1])/s, 0.25f*s, (m[9]+m[6])/s, (m[8]-m[2])/s};
    } else {
        const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q = {(m[8]+m[2])/s, (m[9]+m[6])/s, 0.25f*s, (m[1]-m[4])/s};
    }
    return Normalize(q);
}

Quaternion Slerp(Quaternion a, Quaternion b, float t) {
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (dot < 0) { b = {-b.x,-b.y,-b.z,-b.w}; dot = -dot; }
    if (dot > 0.9995f)
        return Normalize({a.x+t*(b.x-a.x), a.y+t*(b.y-a.y),
                          a.z+t*(b.z-a.z), a.w+t*(b.w-a.w)});
    const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float divisor = std::sin(angle);
    const float x = std::sin((1-t)*angle)/divisor;
    const float y = std::sin(t*angle)/divisor;
    return {a.x*x+b.x*y, a.y*x+b.y*y, a.z*x+b.z*y, a.w*x+b.w*y};
}

void QuaternionToMatrix(Quaternion q, float m[16]) {
    q = Normalize(q);
    const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z, xy=q.x*q.y;
    const float xz=q.x*q.z, yz=q.y*q.z, wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    m[0]=1-2*(yy+zz); m[1]=2*(xy+wz); m[2]=2*(xz-wy); m[3]=0;
    m[4]=2*(xy-wz); m[5]=1-2*(xx+zz); m[6]=2*(yz+wx); m[7]=0;
    m[8]=2*(xz+wy); m[9]=2*(yz-wx); m[10]=1-2*(xx+yy); m[11]=0;
    m[15]=1;
}
}  // namespace

void MultiplyMatrix(const float left[16], const float right[16], float output[16]) {
    float result[16] = {};
    for (int column=0; column<4; ++column)
        for (int row=0; row<4; ++row)
            for (int index=0; index<4; ++index)
                result[column*4+row] += left[index*4+row] * right[column*4+index];
    for (int i=0; i<16; ++i) output[i]=result[i];
}

void InvertRigidTransform(const float input[16], float output[16]) {
    for (int column=0; column<3; ++column) {
        for (int row=0; row<3; ++row) output[column*4+row]=input[row*4+column];
        output[column*4+3]=0;
    }
    output[3]=output[7]=output[11]=0; output[15]=1;
    for (int row=0; row<3; ++row)
        output[12+row]=-(output[row]*input[12]+output[4+row]*input[13]+output[8+row]*input[14]);
}

void InterpolateRigidTransform(const float previous[16], const float current[16],
                               float alpha, float output[16]) {
    alpha=std::clamp(alpha,0.0f,1.0f);
    QuaternionToMatrix(Slerp(MatrixToQuaternion(previous),
                             MatrixToQuaternion(current),alpha),output);
    output[12]=previous[12]+(current[12]-previous[12])*alpha;
    output[13]=previous[13]+(current[13]-previous[13])*alpha;
    output[14]=previous[14]+(current[14]-previous[14])*alpha;
}

void ApplyPresentationCameraDelta(float worldMatrix[16],
                                  const float simulationCamera[16],
                                  const float presentationCamera[16]) {
    if (!worldMatrix || !simulationCamera || !presentationCamera) return;
    float inverse[16]={}, delta[16]={};
    InvertRigidTransform(simulationCamera,inverse);
    MultiplyMatrix(presentationCamera,inverse,delta);
    MultiplyMatrix(delta,worldMatrix,worldMatrix);
}

}  // namespace high_frequency_viewmodel
