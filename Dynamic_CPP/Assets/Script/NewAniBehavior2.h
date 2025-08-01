#include "Core.Minimal.h"
#include "AniBehavior.h"

class NewAniBehavior2 : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(NewAniBehavior2)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
};
