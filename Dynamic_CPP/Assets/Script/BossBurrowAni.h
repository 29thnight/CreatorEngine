#include "Core.Minimal.h"
#include "AniBehavior.h"

class GameObject;
class BossBurrowAni : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(BossBurrowAni)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

	GameObject* m_boss = nullptr;
};
