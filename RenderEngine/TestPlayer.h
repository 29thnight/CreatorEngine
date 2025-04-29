#pragma once
#include "GameObject.h"
#include "Transform.h"
#include "aniState.h"
#include "aniFSM.h"
#include "AniBehaviour.h"
class TestPlayer : Component 
{

public:
	void GetPlayer(GameObject* _player);
	void Update(float deltaTime);
	GameObject* player;
	float speed = 0.1f;

	float maxSpeed = 1.5f;
};

class IdleAni : public AniBehaviour
{
public:
	IdleAni() { name = "Idle"; };
	virtual void Enter() override
	{
		Owner->GetAnimator()->SetAnimation(0);
	};
	virtual void Update(float DeltaTime) override
	{

	};
	virtual void Exit() override
	{

	};
};
class WalkAni : public AniBehaviour
{
public:
	WalkAni() { name = "Walk"; }
	virtual void Enter() override
	{
		Owner->GetAnimator()->SetAnimation(2);
	};
	virtual void Update(float DeltaTime) override
	{

	}; 
	virtual void Exit() override
	{

	};
};

class RunAni : public AniBehaviour
{
public:
	RunAni()  {name = "Run";}
	virtual void Enter() override
	{
		Owner->GetAnimator()->nextAnimIndex =1;
		Owner->GetAnimator()->SetAnimation(1); 
	};
	virtual void Update(float DeltaTime)override
	{

	};
	virtual void Exit()override
	{

	};
};