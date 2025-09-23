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
	// �����Ϳ��� ����(�÷��̾� ��ȣ, �� ��Ŀ/������Ʈ)
	[[Property]]
	int playerIndex{ 0 }; // 0: 1P, 1: 2P

	// ������ ������(��ũ��/ĵ���� ��Ŀ ��ǥ)
	// - �����Ϳ��� ���� ������ �ְų�, ��� ������Ʈ�� RectTransform�κ��� Start���� �о�͵� ��
	Mathf::Vector2 neutralPos{};
	Mathf::Vector2 leftPos{};
	Mathf::Vector2 rightPos{};
private:
	class RectTransformComponent*	iconRect{ nullptr };
	class PlayerSelector*			m_selector{ nullptr };

	// Ʈ�� ���� ������
	bool m_animating{ false };
	bool useEaseOutQuad{ true };
	float m_elapsed{ 0.f };
	float duration{ 0.2f }; // �̵� ���� �ð�(��)
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
