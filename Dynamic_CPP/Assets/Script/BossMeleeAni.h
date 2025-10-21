#include "Core.Minimal.h"
#include "AniBehavior.h"

class Animator;
class GameObject;
class BossMeleeAni : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(BossMeleeAni)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

private:
	Animator* animator = nullptr;
	GameObject* ownerObj = nullptr;
};
