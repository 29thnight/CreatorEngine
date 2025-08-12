#include "Core.Minimal.h"
#include "AniBehavior.h"

class PlayerAttackAni : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerAttackAni)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
};
