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
	//<- 여기 뭘 넣어야 되지?
	std::array<int, 2> m_playerDir{ 0, 0 }; // -1: left, 1: right <- 컨테이너 인덱스는 player1, player2를 의미;
};
