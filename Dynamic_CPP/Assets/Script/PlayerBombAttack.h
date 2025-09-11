#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerBombAttack : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerBombAttack)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
	Player* m_player;
};
