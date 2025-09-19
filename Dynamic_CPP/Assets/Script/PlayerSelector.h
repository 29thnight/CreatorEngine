#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class PlayerSelector : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(PlayerSelector)
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

private:
	//<- ���� �� �־�� ����?
	//std::array<int, 2> m_playerIndex{ -1, -1 }; // 0: player1, 1: player2 <- �̰� ���࿡ PlayerInputComponent�� ������� ���� �� �ۼ�
	std::array<int, 2> m_playerDir{ -1, -1 }; // 0: left, 1: right <- �����̳� �ε����� player1, player2�� �ǹ�;
};
