#include "Core.Minimal.h"
#include "AniBehavior.h"

class PlayerBombCharing : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerBombCharing)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
};
