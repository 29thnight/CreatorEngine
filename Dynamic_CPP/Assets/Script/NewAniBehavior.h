#include "Core.Minimal.h"
#include "AniBehavior.h"

class NewAniBehavior : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(NewAniBehavior)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
};
