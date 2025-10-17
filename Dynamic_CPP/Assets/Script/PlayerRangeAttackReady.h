#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerRangeAttackReady : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerRangeAttackReady)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
	Player* m_player;
};
