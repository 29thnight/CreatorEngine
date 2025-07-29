#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include "Core.Minimal.h"
using namespace std;
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
		return 1 - cos(t * (M_PI / 2));
	}
	inline static float EaseOutSine(float t) {
		return sin(t * (M_PI / 2));
	}
	inline static float EaseInOutSine(float t) {
		return -0.5f * (cos(M_PI * t) - 1);
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
		return -(a * pow(2, 10 * (t - 1)) * sin((t - 1 - s) * (2 * M_PI) / p));
	}
	inline static float EaseOutElastic(float t, float a = 1, float p = 0.3f) {
		if (t == 0 || t == 1) return t;
		float s = p / 4;
		return a * pow(2, -10 * t) * sin((t - s) * (2 * M_PI) / p) + 1;
	}
	inline static float EaseInOutElastic(float t, float a = 1, float p = 0.3f) {
		if (t == 0 || t == 1) return t;
		float s = p / 4;
		if (t < 0.5f) {
			return -0.5f * (a * pow(2, 20 * t - 10) * sin((20 * t - 11.125) * (2 * M_PI) / p));
		}
		else {
			return a * pow(2, -20 * t + 10) * sin((20 * t - 11.125) * (2 * M_PI) / p) + 1;
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
		return -(a * pow(2, 10 * (t - 1)) * sin((t - 1 - s) * (2 * M_PI) / p));
	}
	inline static float EaseOutElasticCustom(float t, float a = 1, float p = 0.3f) {
		if (t == 0 || t == 1) return t;
		float s = p / 4;
		return a * pow(2, -10 * t) * sin((t - s) * (2 * M_PI) / p) + 1;
	}
	inline static float EaseInOutElasticCustom(float t, float a = 1, float p = 0.3f) {
		if (t == 0 || t == 1) return t;
		float s = p / 4;
		if (t < 0.5f) {
			return -0.5f * (a * pow(2, 20 * t - 10) * sin((20 * t - 11.125) * (2 * M_PI) / p));
		}
		else {
			return a * pow(2, -20 * t + 10) * sin((20 * t - 11.125) * (2 * M_PI) / p) + 1;
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
//class DOTween {
//public:
//    template <typename T>
//    static Tween To(std::function<T()> getter, std::function<void(T)> setter, T endValue, float duration) {
//        // Tweener를 shared_ptr로 생성
//        auto tweenerPtr = std::make_shared<Tweener<T>>(getter, setter, endValue, duration);
//
//        // 관리자에 등록
//		GameObject::Find("GameManager")->GetComponent<TweenManager>()->AddTween(tweenerPtr);
//
//        // 사용자에게는 제어용 핸들(Tween)을 반환
//        return Tween(tweenerPtr);
//    }
//
//	template<>
//	static Tween To(std::function<Mathf::Vector3()> getter, std::function<void(Mathf::Vector3)> setter, Mathf::Vector3 endValue, float duration) {
//		// Tweener를 shared_ptr로 생성
//		auto tweenerPtr = std::make_shared<Tweener<Mathf::Vector3>>(getter, setter, endValue, duration);
//
//		// 관리자에 등록
//		GameObject::Find("GameManager")->GetComponent<TweenManager>()->AddTween(tweenerPtr);
//
//		// 사용자에게는 제어용 핸들(Tween)을 반환
//		return Tween(tweenerPtr);
//	}
//
//};