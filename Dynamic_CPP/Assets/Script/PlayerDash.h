#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerDash : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerDash)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

	Player* m_player = nullptr;
};
