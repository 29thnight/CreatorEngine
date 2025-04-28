#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "aniState.generated.h"

class aniFSM;

class aniState
{	
public:
    ReflectaniState
	[[Serializable]]
    aniState() = default;
	aniState(aniFSM* Owner, std::string Name) : Owner(Owner), Name(Name) {}
	virtual void Enter() {};
	virtual void Update(float DeltaTime) {};
	virtual void Exit() {};
	[[Property]]
	std::string Name{};
	aniFSM* Owner{};
	[[Property]]
	std::vector<std::shared_ptr<AniTransition>> Transitions;
	[[Property]]
	int index =0; 
};

