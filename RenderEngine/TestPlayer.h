#pragma once
#include "GameObject.h"
#include "Transform.h"
#include "AnimationState.h"
#include "AnimationController.h"
#include "AniBehaviour.h"
#include "Animator.h"
class TestPlayer : Component 
{

public:
	void GetPlayer(GameObject* _player);
	void Update(float deltaTime);
	GameObject* player;
	float speed = 0.1f;
	float maxSpeed = 15.0f;
};

class IdleAni : public AniBehaviour
{
public:
	IdleAni() { name = "Idle"; };
	virtual void Enter() override
	{

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
	};
	virtual void Update(float DeltaTime)override
	{

	};
	virtual void Exit()override
	{

	};
};