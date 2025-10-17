#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerRangeAttackEnd : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerRangeAttackEnd)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
	Player* m_player;
};
