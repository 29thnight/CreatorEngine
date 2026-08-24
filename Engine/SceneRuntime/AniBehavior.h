#pragma once
#include "TypeTrait.h"

class AnimationController;
class AniBehavior
{
public:
	AniBehavior() = default;
	virtual ~AniBehavior() = default;

	virtual void Enter() 
	{
	};
	virtual void Update(float deltaTime) {};
	virtual void Exit() {};
	AnimationController* m_ownerController{};
	std::string m_name{};
	HashedGuid m_typeID{};
	HashedGuid m_scriptTypeID{};
};