#include "aniState.h"
#include "AnimationBehviourFatory.h"

void aniState::SetBehaviour(std::string name)
{
	behaviour = AnimationFactorys->CreateBehaviour(name);
	behaviourName = behaviour->name;
	behaviour->Owner = this->Owner;
}
