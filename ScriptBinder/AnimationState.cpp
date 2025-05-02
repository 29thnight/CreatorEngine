#include "AnimationState.h"
#include "AnimationBehviourFatory.h"

void AnimationState::SetBehaviour(std::string name)
{
	behaviour = AnimationFactorys->CreateBehaviour(name);
	behaviourName = behaviour->name;
	behaviour->m_ownerController = this->m_ownerController;
}
