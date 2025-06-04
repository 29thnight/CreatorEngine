#include "AnimationState.h"
#include "AnimationBehviourFatory.h"

AnimationState::~AnimationState()
{
	if (behaviour)
		delete behaviour;
}


void AnimationState::SetBehaviour(std::string name)
{
	behaviour = AnimationFactorys->CreateBehaviour(name);
	if(behaviour == nullptr)
		return;
	behaviourName = behaviour->name;
	behaviour->m_ownerController = this->m_ownerController;
}
