#pragma once
#include "../Utility_Framework/Core.Minimal.h"
class aniFSM;

class aniState
{
	
	
public:
	aniState(aniFSM* Owner, std::string Name) : Owner(Owner), Name(Name) {}
	virtual void Enter() = 0;
	virtual void Update(float DeltaTime) = 0; 
	virtual void Exit() = 0;	
	std::string Name;
	aniFSM* Owner;
};

