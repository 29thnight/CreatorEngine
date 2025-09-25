#include "Core.Minimal.h"
#include "AniBehavior.h"

class Player;
class PlayerThrow : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerThrow)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

	Player* m_player = nullptr;
};
