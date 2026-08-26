#pragma once
#include "Core.Definition.h"

namespace Mathf
{
	constexpr float halfPi = 1.57079632679489661923f;
    constexpr float pi = 3.14159265358979323846f;
    constexpr float pi2 = 6.28318530717958647692f;

    using xMatrix = DirectX::XMMATRIX;
    using xVector = DirectX::XMVECTOR;
    using Vector2 = DirectX::SimpleMath::Vector2;
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Vector4 = DirectX::SimpleMath::Vector4;
    using Matrix = DirectX::SimpleMath::Matrix;
    using Quaternion = DirectX::SimpleMath::Quaternion;

    constexpr float Deg2Rad = pi / 180.0f;
    constexpr float Rad2Deg = 180.0f / pi;

    inline Vector3 ExtractScale(const Matrix& matrix)
    {
        // 첫 번째 열 벡터 (X축)
        float scaleX = matrix.Right().Length();

        // 두 번째 열 벡터 (Y축)
        float scaleY = matrix.Up().Length();

        // 세 번째 열 벡터 (Z축)
        float scaleZ = matrix.Forward().Length();

        // 스케일 벡터 반환
        return Vector3(scaleX, scaleY, scaleZ);
    }

    inline float GetFloatAtIndex(DirectX::XMFLOAT4& vec, int i)
    {
        switch (i)
        {
        case 0:
            return vec.x;
        case 1:
            return vec.y;
        case 2:
            return vec.z;
        case 3:
            return vec.w;
        }
        return 0.0;
    };

    inline void SetFloatAtIndex(DirectX::XMFLOAT4& vec, int i, float val)
    {
        switch (i)
        {
        case 0:
            vec.x = val;
            return;
        case 1:
            vec.y = val;
            return;
        case 2:
            vec.z = val;
            return;
        case 3:
            vec.w = val;
            return;
        }
    };

    //Mathf::QuaternionToEular 함수는 쿼터니언을 입력으로 받아서 피치, 요, 롤 각도를 계산하여 참조로 전달합니다.
	inline void QuaternionToEular(const Quaternion& quaternion, float& pitch, float& yaw, float& roll)
	{
        // 1. 쿼터니언을 회전 행렬로 변환
        DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationQuaternion(quaternion);

        // 2. 행렬에서 Euler 각 추출
        pitch = asinf(-rotationMatrix.r[2].m128_f32[1]);  // -m21 (Z 축 Y 값)

        if (cosf(pitch) > 0.0001f) // Gimbal Lock 방지
        {
            yaw = atan2f(rotationMatrix.r[2].m128_f32[0], rotationMatrix.r[2].m128_f32[2]); // m11, m31
            roll = atan2f(rotationMatrix.r[0].m128_f32[1], rotationMatrix.r[1].m128_f32[1]); // m12, m22
        }
        else
        {
            // Gimbal Lock 상태일 때 yaw와 roll을 단순 계산
            yaw = 0.0f;
            roll = atan2f(-rotationMatrix.r[0].m128_f32[2], rotationMatrix.r[0].m128_f32[0]); // -m13, m11
        }
	}
    inline float ToRadians(float degrees)
    {
        return degrees * Mathf::Deg2Rad;
	}

}
