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
	constexpr float halfPi = 1.57079632679489661923;
    constexpr float pi = 3.14159265358979323846;
    constexpr float pi2 = 6.28318530717958647692;

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

	class Easing {
	public:
		inline static float Linear(float t) {
			return t;
		}
		inline static float EaseInQuad(float t) {
			return t * t;
		}
		inline static float EaseOutQuad(float t) {
			return t * (2 - t);
		}
		inline static float EaseInOutQuad(float t) {
			if (t < 0.5f) {
				return 2 * t * t;
			}
			else {
				return -1 + (4 - 2 * t) * t;
			}
		}
		inline static float EaseInCubic(float t) {
			return t * t * t;
		}
		inline static float EaseOutCubic(float t) {
			return --t * t * t + 1;
		}
		inline static float EaseInOutCubic(float t) {
			if (t < 0.5f) {
				return 4 * t * t * t;
			}
			else {
				t -= 1;
				return 1 + 4 * t * t * t;
			}
		}
		inline static float EaseInQuart(float t) {
			return t * t * t * t;
		}
		inline static float EaseOutQuart(float t) {
			return 1 - (--t * t * t * t);
		}
		inline static float EaseInOutQuart(float t) {
			if (t < 0.5f) {
				return 8 * t * t * t * t;
			}
			else {
				t -= 1;
				return 1 - 8 * t * t * t * t;
			}
		}
		inline static float EaseInQuint(float t) {
			return t * t * t * t * t;
		}
		inline static float EaseOutQuint(float t) {
			return 1 + (--t * t * t * t * t);
		}
		inline static float EaseInOutQuint(float t) {
			if (t < 0.5f) {
				return 16 * t * t * t * t * t;
			}
			else {
				t -= 1;
				return 1 + 16 * t * t * t * t * t;
			}
		}
		inline static float EaseInSine(float t) {
			return 1 - cos(t * (Mathf::pi / 2));
		}
		inline static float EaseOutSine(float t) {
			return sin(t * (Mathf::pi / 2));
		}
		inline static float EaseInOutSine(float t) {
			return -0.5f * (cos(Mathf::pi * t) - 1);
		}
		inline static float EaseInExpo(float t) {
			return (t == 0) ? 0 : pow(2, 10 * (t - 1));
		}
		inline static float EaseOutExpo(float t) {
			return (t == 1) ? 1 : 1 - pow(2, -10 * t);
		}
		inline static float EaseInOutExpo(float t) {
			if (t == 0 || t == 1) return t;
			if (t < 0.5f) {
				return 0.5f * pow(2, 20 * t - 10);
			}
			else {
				return 1 - 0.5f * pow(2, -20 * t + 10);
			}
		}
		inline static float EaseInCirc(float t) {
			return 1 - sqrt(1 - t * t);
		}
		inline static float EaseOutCirc(float t) {
			t -= 1;
			return sqrt(1 - t * t);
		}
		inline static float EaseInOutCirc(float t) {
			if (t < 0.5f) {
				return 0.5f * (1 - sqrt(1 - 4 * t * t));
			}
			else {
				t -= 1;
				return 0.5f * (sqrt(1 - 4 * t * t) + 1);
			}
		}
		inline static float EaseInBack(float t, float s = 1.70158f) {
			return t * t * ((s + 1) * t - s);
		}
		inline static float EaseOutBack(float t, float s = 1.70158f) {
			t -= 1;
			return t * t * ((s + 1) * t + s) + 1;
		}
		inline static float EaseInOutBack(float t, float s = 1.70158f) {
			if (t < 0.5f) {
				t *= 2;
				return 0.5f * (t * t * ((s + 1) * t - s));
			}
			else {
				t = 2 * t - 2;
				return 0.5f * (t * t * ((s + 1) * t + s) + 2);
			}
		}
		inline static float EaseInElastic(float t, float a = 1, float p = 0.3f) {
			if (t == 0 || t == 1) return t;
			float s = p / 4;
			return -(a * pow(2, 10 * (t - 1)) * sin((t - 1 - s) * (2 * Mathf::pi) / p));
		}
		inline static float EaseOutElastic(float t, float a = 1, float p = 0.3f) {
			if (t == 0 || t == 1) return t;
			float s = p / 4;
			return a * pow(2, -10 * t) * sin((t - s) * (2 * Mathf::pi) / p) + 1;
		}
		inline static float EaseInOutElastic(float t, float a = 1, float p = 0.3f) {
			if (t == 0 || t == 1) return t;
			float s = p / 4;
			if (t < 0.5f) {
				return -0.5f * (a * pow(2, 20 * t - 10) * sin((20 * t - 11.125) * (2 * Mathf::pi) / p));
			}
			else {
				return a * pow(2, -20 * t + 10) * sin((20 * t - 11.125) * (2 * Mathf::pi) / p) + 1;
			}
		}
		inline static float EaseInBounce(float t) {
			return 1 - EaseOutBounce(1 - t);
		}
		inline static float EaseOutBounce(float t) {
			if (t < (1 / 2.75f)) {
				return 7.5625f * t * t;
			}
			else if (t < (2 / 2.75f)) {
				t -= (1.5f / 2.75f);
				return 7.5625f * t * t + 0.75f;
			}
			else if (t < (2.5 / 2.75)) {
				t -= (2.25f / 2.75f);
				return 7.5625f * t * t + 0.9375f;
			}
			else {
				t -= (2.625f / 2.75f);
				return 7.5625f * t * t + 0.984375f;
			}
		}
		inline static float EaseInOutBounce(float t) {
			if (t < 0.5f) {
				return 0.5f * EaseInBounce(t * 2);
			}
			else {
				return 0.5f * EaseOutBounce(t * 2 - 1) + 0.5f;
			}
		}
		inline static float EaseInElasticCustom(float t, float a = 1, float p = 0.3f) {
			if (t == 0 || t == 1) return t;
			float s = p / 4;
			return -(a * pow(2, 10 * (t - 1)) * sin((t - 1 - s) * (2 * Mathf::pi) / p));
		}
		inline static float EaseOutElasticCustom(float t, float a = 1, float p = 0.3f) {
			if (t == 0 || t == 1) return t;
			float s = p / 4;
			return a * pow(2, -10 * t) * sin((t - s) * (2 * Mathf::pi) / p) + 1;
		}
		inline static float EaseInOutElasticCustom(float t, float a = 1, float p = 0.3f) {
			if (t == 0 || t == 1) return t;
			float s = p / 4;
			if (t < 0.5f) {
				return -0.5f * (a * pow(2, 20 * t - 10) * sin((20 * t - 11.125) * (2 * Mathf::pi) / p));
			}
			else {
				return a * pow(2, -20 * t + 10) * sin((20 * t - 11.125) * (2 * Mathf::pi) / p) + 1;
			}
		}
		inline static float EaseInBounceCustom(float t) {
			return 1 - EaseOutBounceCustom(1 - t);
		}
		inline static float EaseOutBounceCustom(float t) {
			if (t < (1 / 2.75f)) {
				return 7.5625f * t * t;
			}
			else if (t < (2 / 2.75f)) {
				t -= (1.5f / 2.75f);
				return 7.5625f * t * t + 0.75f;
			}
			else if (t < (2.5 / 2.75)) {
				t -= (2.25f / 2.75f);
				return 7.5625f * t * t + 0.9375f;
			}
			else {
				t -= (2.625f / 2.75f);
				return 7.5625f * t * t + 0.984375f;
			}
		}
		inline static float EaseInOutBounceCustom(float t) {
			if (t < 0.5f) {
				return 0.5f * EaseInBounceCustom(t * 2);
			}
			else {
				return 0.5f * EaseOutBounceCustom(t * 2 - 1) + 0.5f;
			}
		}
	};


	template<typename T>
	struct LerpHelper;

	// float 타입에 대한 템플릿 특수화
	template<>
	struct LerpHelper<float> {
		static float Apply(const float& start, const float& end, float t) {
			return start + (end - start) * t;
		}
	};

	// Vector3 타입에 대한 템플릿 특수화
	template<>
	struct LerpHelper<Mathf::Vector3> {
		static Mathf::Vector3 Apply(const Mathf::Vector3& start, const Mathf::Vector3& end, float t) {
			return Mathf::Vector3::Lerp(start, end, t);
		}
	};

	//template<typename T>
	struct ITween {
		virtual ~ITween() = default;
		virtual bool Update(float deltaTime) = 0;
		virtual void Pause() = 0;
		virtual void Resume() = 0;
		virtual void Kill() = 0;
		virtual bool IsAlive() const = 0;
	};

	template <typename T>
	class Tweener : public ITween {
	public:
		enum class State { Playing, Paused, Killed };

	private:
		std::function<T()> getter;
		std::function<void(T)> setter;
		T endValue;
		float duration;
		std::function<float(float)> easeFunction;
		std::function<void()> onCompleteCallback;

		T startValue;
		float elapsedTime = 0.0f;
		State currentState = State::Playing;

	public:
		Tweener(std::function<T()> getter, std::function<void(T)> setter, T endValue, float duration, std::function<float(float)> easeFunction = Easing::Linear)
			: getter(getter), setter(setter), endValue(endValue), duration(duration), easeFunction(easeFunction) {
			this->startValue = this->getter();
		}


		bool Update(float deltaTime) override {
			if (currentState != State::Playing) return IsAlive();

			elapsedTime += deltaTime;
			float progress = std::min(1.0f, duration > 0 ? elapsedTime / duration : 1.0f);

			float easedProgress = easeFunction(progress);

			setter(LerpHelper<T>::Apply(startValue, endValue, easedProgress));

			if (progress >= 1.0f) {
				Kill();
				if (onCompleteCallback) {
					onCompleteCallback();
				}
			}
			return IsAlive();
		}

		void Pause() override { if (currentState == State::Playing) currentState = State::Paused; }
		void Resume() override { if (currentState == State::Paused) currentState = State::Playing; }
		void Kill() override { currentState = State::Killed; }
		bool IsAlive() const override { return currentState != State::Killed; }

		// 메서드 체이닝을 위한 함수들
		Tweener* SetEase(std::function<float(float)> func) { this->easeFunction = func; return this; }
		Tweener* SetOnComplete(std::function<void()> func) { this->onCompleteCallback = func; return this; }
	};
	//#include "SceneManager.h"
	//#include "GameObject.h"
	//#include "TweenManager.h"
	class Tween {
	private:
		std::shared_ptr<ITween> tweenPtr;

	public:
		Tween(std::shared_ptr<ITween> ptr) : tweenPtr(ptr) {}

		void Pause() { if (tweenPtr) tweenPtr->Pause(); }
		void Resume() { if (tweenPtr) tweenPtr->Resume(); }
		void Kill() { if (tweenPtr) tweenPtr->Kill(); }

		// SetEase, SetOnComplete 등을 체이닝하고 싶다면
		// ITween에 가상 메서드를 추가하고 여기서 호출해주면 됩니다.
		// 예: Tween& SetEase(...) { ...; return *this; }
	};
}
