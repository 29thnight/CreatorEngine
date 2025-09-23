#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class PlayerSelector : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(PlayerSelector)
	virtual void Start() override;
	virtual void Update(float tick) override;

	void MoveSelector(int playerIndex, int dir);

private:
	//<- ���� �� �־�� ����?
	std::array<int, 2> m_playerDir{ 0, 0 }; // -1: left, 1: right <- �����̳� �ε����� player1, player2�� �ǹ�;
};
