#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerRangeAttack : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerRangeAttack)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
	Player* m_player;
};
