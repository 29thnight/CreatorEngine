#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "AnimationState.generated.h"
class AnimationController;
class AniBehaviour;
class AnimationState
{	
public:
   ReflectAnimationState
	[[Serializable]]
	AnimationState() = default;
	AnimationState(AnimationController* Owner, std::string Name) : m_ownerController(Owner), Name(Name) {}


	void SetBehaviour(std::string name);
	[[Property]]
	std::string Name{};

	std::string behaviourName{};
	AniBehaviour* behaviour{};
	AnimationController* m_ownerController{};
	[[Property]]
	std::vector<std::shared_ptr<AniTransition>> Transitions;
	[[Property]]
	int index =0; 
	[[Property]]
	int AnimationIndex = -1;
};

