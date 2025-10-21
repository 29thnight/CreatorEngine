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
	// 이 상태에 머문 시간을 측정하기 위한 타이머입니다.
	float m_stateTimer = 0.0f;
};
