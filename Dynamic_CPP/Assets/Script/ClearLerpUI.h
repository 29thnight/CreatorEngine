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

	bool  m_fadePending = false;   // ������ ��
	bool  m_fadeRunning = false;   // ���� ��
	bool  m_fadeJustDone = false;   // �̹� �����ӿ� �� �Ϸ��(������)

	float m_fadeDelay = 0.0f;    // ���۱��� ��� �ð�
	float m_fadeDuration = 0.5f;    // ���� �ð�
	float m_fadeElapsed = 0.0f;    // ���� ����

	Mathf::Vector3 m_fadeFrom = { 1.f,1.f,1.f };
	Mathf::Vector3 m_fadeTo = { 0.f,0.f,0.f };
};
