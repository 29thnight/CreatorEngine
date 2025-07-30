#pragma once
class AnimationController;
class AniBehaviour
{
public:
	AniBehaviour() {};
	virtual void Enter() {};
	virtual void Update(float deltaTime) {};
	virtual void Exit() {};
	AnimationController* m_ownerController{};
};