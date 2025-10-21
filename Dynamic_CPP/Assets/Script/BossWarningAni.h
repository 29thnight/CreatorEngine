#include "Core.Minimal.h"
#include "AniBehavior.h"

class Animator;
class GameObject;
class BossWarningAni : public AniBehavior
{
public:
	ANIBEHAVIOR_BODY(BossWarningAni)
	virtual void Enter() override;
	virtual void Update(float tick) override;
	virtual void Exit() override;
private:
	Animator* animator = nullptr;
	GameObject* ownerObj = nullptr;
	// �� ���¿� �ӹ� �ð��� �����ϱ� ���� Ÿ�̸��Դϴ�.
	float m_stateTimer = 0.0f;
};
