#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

enum class PauseWindowType : uint32
{
	Self = 0,     // Pause �޴�(����)
	Settings = 1, // SettingPanel
	HowTo = 2,    // HowToPlayPanel
};

class PauseMenuButton : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(PauseMenuButton)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void OnEnable() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override;
	virtual void OnDestroy() override  {}

	// �ʱ�/����/��ȯ
	void ComputeOffscreenPositions();
	void ApplyTargets(float tick);
	void SetActive(PauseWindowType next);
	void Next(); // RT/��
	void Prev(); // LT/��

private:
	class GameObject*		m_pauseMenuCanvasObject{};
	class ImageComponent*	m_boxImageComponent{};
	bool					m_isMenuOpened{ false };

private:
	class GameObject*		m_settingPanelObject{};
	class GameObject*		m_howToPlayPanelObject{};

	class RectTransformComponent* m_selfRect{ nullptr };
	class RectTransformComponent* m_settingsRect{ nullptr };
	class RectTransformComponent* m_howToRect{ nullptr };

	PauseWindowType m_active{ PauseWindowType::Self };
	bool m_isTransitioning{ false };

	// ���̾ƿ�/���� �Ķ����
	float m_slideSpeed{ 10.0f };  // ���� ���� �ӵ�
	float m_margin{ 30.0f };
	Mathf::Vector2 m_centerPos{ 1920.f * 0.5f, 1080.f * 0.5f };
	Mathf::Vector2 m_offLeftPos{ -1300.f, 1080.f * 0.5f };
	Mathf::Vector2 m_offRightPos{ 3220.f, 1080.f * 0.5f };

	// �Է� ��ٿ(Ʈ����/Ű �ٿ� �ߺ� ��ȯ ����)
	bool m_leftHeld{ false };
	bool m_rightHeld{ false };
};
