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

	// 추가: 두 패널(옵션/조작법)과 RectTransform 핸들
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

	// 애니메이션 파라미터
	[[Property]]
	float m_slideSpeed = 64.0f;    // 커질수록 빨리 붙음 (lerp 계수)
	float m_margin = 64.f;        // 바깥으로 빼는 여유
	bool m_isEntering{ false };

private:
	void ComputeOffscreenPositions();
	void ApplyTargets(float tick);
	void SetActive(SettingWindowType next);
};
