#pragma once
#include "GameObject.h"
#include "Transform.h"
#include "aniState.h"
#include "aniFSM.h"
class TestPlayer
{

public:
	void GetPlayer(GameObject* _player);
	void Update(float deltaTime);
	GameObject* player;
	float speed = 0.1f;

	float maxSpeed = 1.5f;
};

class IdleAni : public aniState
{
public:
	virtual void Enter()
	{
		Owner->GetAnimator()->SetAnimation(0);
	};
	virtual void Update(float DeltaTime)
	{

	};
	virtual void Exit()
	{

	};
};
class WalkAni : public aniState
{
public:
	virtual void Enter()
	{
		Owner->GetAnimator()->SetAnimation(2);
	};
	virtual void Update(float DeltaTime)
	{

	};
	virtual void Exit()
	{

	};
};
class RunAni : public aniState
{
public:
	virtual void Enter()
	{
		Owner->GetAnimator()->SetAnimation(1);
	};
	virtual void Update(float DeltaTime)
	{

	};
	virtual void Exit()
	{

	};
};