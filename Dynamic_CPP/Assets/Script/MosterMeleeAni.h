#include "Core.Minimal.h"
#include "AniBehavior.h"

class MosterMeleeAni : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(MosterMeleeAni)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
};
