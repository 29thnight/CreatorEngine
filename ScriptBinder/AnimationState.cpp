#include "AnimationState.h"
#include "AnimationBehviourFatory.h"

AnimationState::~AnimationState()
{
	if (behaviour)
		delete behaviour;
}


std::vector<AniTransition*> AnimationState::FindTransitions(const std::string& toStateName)
{
	std::vector<AniTransition*> aniTransitions;
	for (auto& sharedtrans : Transitions)
	{
		auto trans = sharedtrans.get();

		if (trans->GetNextState() == toStateName)
			aniTransitions.push_back(trans);
	}
	
	return aniTransitions;
}

void AnimationState::SetBehaviour(std::string name)
{
	behaviour = AnimationFactorys->CreateBehaviour(name);
	if(behaviour == nullptr)
		return;
	behaviourName = behaviour->name;
	behaviour->m_ownerController = this->m_ownerController;
}
