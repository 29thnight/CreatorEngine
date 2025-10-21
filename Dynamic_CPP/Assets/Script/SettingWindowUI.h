#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "SettingWindowUI.generated.h"

class SettingWindowUI : public ModuleBehavior
{
public:
   ReflectSettingWindowUI
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(SettingWindowUI)
	virtual void Start() override;
    virtual void OnEnable() override;
	virtual void Update(float tick) override;
	virtual void OnDisable() override;

	void SetEntering(bool isEntering) { m_isEntering = isEntering; }
	bool IsEntering() const { return m_isEntering; }

	enum class SettingWindowType
	{
		Options,
		HowToPlay,
	};

private:
	class GameObject* m_mainCanvasObj{ nullptr };
	class GameObject* m_settingCanvasObj{ nullptr };
	class ImageComponent* m_backgroundImageComponent{ nullptr };

	// �߰�: �� �г�(�ɼ�/���۹�)�� RectTransform �ڵ�
	class GameObject* m_optionsObj{ nullptr };
	class GameObject* m_howToPlayObj{ nullptr };
	class GameObject* m_settingButtonObj{ nullptr };
	class RectTransformComponent* m_optionsRect{ nullptr };
	class RectTransformComponent* m_howToRect{ nullptr };

	SettingWindowType m_active{ SettingWindowType::Options };
	bool m_isTransitioning{ false };

	Mathf::Vector2 m_centerPos{ 960.f, 540.f };
	Mathf::Vector2 m_offLeftPos{ 0.f, 540.f };
	Mathf::Vector2 m_offRightPos{ 1920.f, 540.f };

	// �ִϸ��̼� �Ķ����
	[[Property]]
	float m_slideSpeed = 64.0f;    // Ŀ������ ���� ���� (lerp ���)
	float m_margin = 64.f;        // �ٱ����� ���� ����
	bool m_isEntering{ false };

private:
	void ComputeOffscreenPositions();
	void ApplyTargets(float tick);
	void SetActive(SettingWindowType next);
};
