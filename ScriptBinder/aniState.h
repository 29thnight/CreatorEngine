#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "aniState.generated.h"
class AnimationController;
class AniBehaviour;
class aniState 
{	
public:
    ReflectaniState
	[[Serializable]]
    aniState() = default;
	aniState(AnimationController* Owner, std::string Name) : Owner(Owner), Name(Name) {}


	void SetBehaviour(std::string name);
	[[Property]]
	std::string Name{};

	std::string behaviourName{};
	AniBehaviour* behaviour{};
	AnimationController* Owner{};
	[[Property]]
	std::vector<std::shared_ptr<AniTransition>> Transitions;
	[[Property]]
	int index =0; 
};

