#pragma once
#include "Core.Definition.h"
#include "Core.Assert.hpp"
#include <nlohmann/json.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace DirectX;

namespace Mathf
{
	const static float halfPi = XM_PIDIV2;
    const static float pi = XM_PI;
    const static float pi2 = XM_PI * 2.f;

    using xMatrix = XMMATRIX;
    using xVector = XMVECTOR;
    using Color3 = SimpleMath::Vector3;
    using Color4 = SimpleMath::Color;
    using xVColor4 = XMVECTORF32;
    using Vector2 = SimpleMath::Vector2;
    using Vector3 = SimpleMath::Vector3;
    using Vector4 = SimpleMath::Vector4;
    using Matrix = SimpleMath::Matrix;
    using Quaternion = SimpleMath::Quaternion;

	static xMatrix xMatrixIdentity = XMMatrixIdentity();
	static xVector xVectorZero = XMVectorZero();
	static xVector xVectorOne = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	static xVector xVectorUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	static xVector xVectorDown = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	static xVector xVectorRight = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	static xVector xVectorLeft = XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f);
	static xVector xVectorForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

    struct Rect
    {
        float x, y, width, height;
        Rect() : x(0), y(0), width(0), height(0) {}
        Rect(float _x, float _y, float _width, float _height) : x(_x), y(_y), width(_width), height(_height) {}
	};

    template<class T> inline T lerp(T low, T high, float t)
    {
        return low + static_cast<T>((high - low) * t);
    }

    template<class T> void ClampedIncrementOrDecrement(T& val, int upOrDown, int lo, int hi)
    {
        int newVal = static_cast<int>(val) + upOrDown;
        if (upOrDown > 0) newVal = newVal >= hi ? (hi - 1) : newVal;
        else              newVal = newVal < lo ? lo : newVal;
        val = static_cast<T>(newVal);
    }

    template<class T> void Clamp(T& val, const T lo, const T hi)
    {
        val = std::max(std::min(val, hi), lo);
    }

	template<class T> void Wrap(T& val, const T lo, const T hi)
	{
		if (val >= hi) val = lo;
		if (val < lo) val = hi;
	}

    template<class T> T Clamp(const T& val, const T lo, const T hi)
    {
        T _val = val;
        Clamp(_val, lo, hi);
        return _val;
    }

    template<typename T> T Lerp(const T& a, const T& b, float t) noexcept
    {
        return a + (b - a) * t;
	}

    inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) noexcept
    {
        return Vector3::Lerp(a, b, t);
    }

    inline Vector2 Lerp(const Vector2& a, const Vector2& b, float t) noexcept
    {
        return Vector2::Lerp(a, b, t);
    }
    inline Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) noexcept
    {
        return Quaternion::Slerp(a, b, t);
    }

    constexpr float Deg2Rad = XM_PI / 180.0f;
    constexpr float Rad2Deg = 180.0f / XM_PI;

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

    static inline Vector3 ConvertToDistance(const Mathf::Vector4& vector) noexcept
    {
        return Vector3(vector.x, vector.y, vector.z);
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

    inline XMUINT4 CreateBoneIndex(aiBone* bone) noexcept
    {
        constexpr uint32 MAX_BONE_COUNT = 4;
        XMUINT4 result{};
        uint32 tempIndexInfo[4]{ 0, 0, 0, 0 };

        for (uint32 i = 0; i < MAX_BONE_COUNT; i++)
        {
            tempIndexInfo[i] = bone->mWeights[i].mVertexId;
        }

        result.x = tempIndexInfo[0];
        result.y = tempIndexInfo[1];
        result.z = tempIndexInfo[2];
        result.w = tempIndexInfo[3];

        return result;
    }

    inline XMFLOAT4 CreateBoneWeight(aiBone* bone) noexcept
    {
        constexpr uint32 MAX_BONE_COUNT = 4;
        XMFLOAT4 result{};
        float tempWeightInfo[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        for (uint32 i = 0; i < MAX_BONE_COUNT; i++)
        {
            tempWeightInfo[i] = bone->mWeights[i].mWeight;
        }

        result.x = tempWeightInfo[0];
        result.y = tempWeightInfo[1];
        result.z = tempWeightInfo[2];
        result.w = tempWeightInfo[3];

        return result;
    }

    inline DirectX::XMMATRIX aiToXMMATRIX(aiMatrix4x4 in)
    {
        return XMMatrixTranspose(XMMATRIX(&in.a1));
    }

    inline float3 aiToFloat3(aiVector3D in)
    {
        return float3(in.x, in.y, in.z);
    }

    inline float2 aiToFloat2(aiVector3D in)
    {
        return float2(in.x, in.y);
    }

	inline Mathf::Vector2 jsonToVector2(const nlohmann::json& j)
	{
		return Mathf::Vector2(j[0].get<float>(), j[1].get<float>());
	}

	inline Mathf::Vector3 jsonToVector3(const nlohmann::json& j)
	{
		return Mathf::Vector3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
	}

	inline Mathf::Color4 jsonToColor4(const nlohmann::json& j)
	{
		return Mathf::Color4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
	}

    //Mathf::QuaternionToEular 함수는 쿼터니언을 입력으로 받아서 피치, 요, 롤 각도를 계산하여 참조로 전달합니다.
	inline void QuaternionToEular(const Quaternion& quaternion, float& pitch, float& yaw, float& roll)
	{
        // 1. 쿼터니언을 회전 행렬로 변환
        XMMATRIX rotationMatrix = XMMatrixRotationQuaternion(quaternion);

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
    inline void EularToQuaternion(float pitch, float yaw, float roll, Quaternion& rotation) {
        float halfDegToRad = 0.5f * Mathf::Deg2Rad;
        pitch = pitch * halfDegToRad;
        yaw = yaw * halfDegToRad;
        roll = roll * halfDegToRad;

        float sinX = std::sin(pitch);
        float cosX = std::cos(pitch);
        float sinY = std::sin(yaw);
        float cosY = std::cos(yaw);
        float sinZ = std::sin(roll);
        float cosZ = std::cos(roll);

        rotation = Quaternion(
            cosY * sinX * cosZ + sinY * cosX * sinZ,
            sinY * cosX * cosZ - cosY * sinX * sinZ,
            cosY * cosX * sinZ - sinY * sinX * cosZ,
            cosY * cosX * cosZ + sinY * sinX * sinZ
        );
    }

	inline float Distance(const Mathf::Vector3& a, const Mathf::Vector3& b)
	{
		return sqrtf(powf(b.x - a.x, 2) + powf(b.y - a.y, 2) + powf(b.z - a.z, 2));
	}

	inline Mathf::Vector3 Normalize(const Mathf::Vector3& vector)
	{
		float length = vector.Length();
		if (length == 0.0f) return Mathf::Vector3::Zero;
		return vector / length;
	}

    inline float ToDegrees(float radians)
    {
        return radians * Mathf::Rad2Deg;
	}

    inline float ToRadians(float degrees)
    {
        return degrees * Mathf::Deg2Rad;
	}
}
