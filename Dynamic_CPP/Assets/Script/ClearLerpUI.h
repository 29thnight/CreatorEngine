#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class ClearLerpUI : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(ClearLerpUI)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}
public:

	void StartFade(float delaySec, float durationSec,
		const Mathf::Vector4& from = { 1.f,1.f,1.f,1.f },
		const Mathf::Vector4& to = { 0.f,0.f,0.f,0.f });

	void OnFadeComplete();

private:
	class ImageComponent* m_image{};
	class GameManager* m_gm{};

	bool  m_fadePending = false;   // 딜레이 중
	bool  m_fadeRunning = false;   // 보간 중
	bool  m_fadeJustDone = false;   // 이번 프레임에 막 완료됨(폴링용)

	float m_fadeDelay = 0.0f;    // 시작까지 대기 시간
	float m_fadeDuration = 0.5f;    // 보간 시간
	float m_fadeElapsed = 0.0f;    // 진행 누적

	Mathf::Vector3 m_fadeFrom = { 1.f,1.f,1.f };
	Mathf::Vector3 m_fadeTo = { 0.f,0.f,0.f };
};
