#pragma once
#include "Core.Definition.h"
#include <nlohmann/json.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace DirectX;

namespace Mathf
{
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

	static xVector xVectorZero = XMVectorZero();
	static xVector xVectorOne = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
	static xVector xVectorUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

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

    inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) noexcept
    {
        return Vector3::Lerp(a, b, t);
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

}
