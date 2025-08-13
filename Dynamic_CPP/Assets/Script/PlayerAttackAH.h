#include "Core.Minimal.h"
#include "AniBehavior.h"

class GameObject;
class Player;
class PlayerAttackAH : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(PlayerAttackAH)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;

	Player* m_player = nullptr;
	GameObject* m_effect = nullptr;
};
