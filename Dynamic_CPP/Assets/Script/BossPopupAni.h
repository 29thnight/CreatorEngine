#include "Core.Minimal.h"
#include "AniBehavior.h"

class GameObject;
class BossPopupAni : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(BossPopupAni)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

	GameObject* m_boss = nullptr;
};
