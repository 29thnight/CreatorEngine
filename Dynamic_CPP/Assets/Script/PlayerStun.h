#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerStun : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerStun)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

	Player* m_player;
};
