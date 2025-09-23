#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "PlayerSelectorTypes.h"
#include "MovingUILayer.generated.h"

class MovingUILayer : public ModuleBehavior, public IPlayerSelectorObserver
{
public:
   ReflectMovingUILayer
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(MovingUILayer)
	virtual void Start() override;
	virtual void Update(float tick) override;
	virtual void OnDestroy() override;

	virtual void OnSelectorChanged(const SelectorState& state) override;

private:
	// 에디터에서 세팅(플레이어 번호, 각 앵커/오브젝트)
	[[Property]]
	int playerIndex{ 0 }; // 0: 1P, 1: 2P

	// 목적지 포지션(스크린/캔버스 앵커 좌표)
	// - 에디터에서 직접 값으로 넣거나, 대상 오브젝트의 RectTransform로부터 Start에서 읽어와도 됨
	Mathf::Vector2 neutralPos{};
	Mathf::Vector2 leftPos{};
	Mathf::Vector2 rightPos{};
private:
	class RectTransformComponent*	iconRect{ nullptr };
	class PlayerSelector*			m_selector{ nullptr };

	// 트윈 러닝 데이터
	bool m_animating{ false };
	bool useEaseOutQuad{ true };
	float m_elapsed{ 0.f };
	float duration{ 0.2f }; // 이동 지속 시간(초)
	Mathf::Vector2 m_startPos{};
	Mathf::Vector2 m_targetPos{};
	SelectorSlot   m_curSlot{ SelectorSlot::Neutral };

	inline Mathf::Vector2 SlotToPos(SelectorSlot s) const
	{
		switch (s) {
		case SelectorSlot::Left:    return leftPos;
		case SelectorSlot::Right:   return rightPos;
		case SelectorSlot::Neutral:
		default:                    return neutralPos;
		}
	}
};
