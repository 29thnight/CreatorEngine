#include "AnimationState.h"
#include "AniBehavior.h"
#include "HotLoadSystem.h"
#include "AnimationBehviourFatory.h"
#include "AnimationController.h"
#include "Animator.h"
#include "ConditionParameter.h"
AnimationState::AnimationState()
{
	ScriptManager->CollectAniBehavior(this);
}

AnimationState::~AnimationState()
{
	behaviour.reset();
	ScriptManager->UnCollectAniBehavior(this);
}

AnimationState::AnimationState(AnimationController* Owner, std::string name) : m_ownerController(Owner), m_name(name)
{
	ScriptManager->CollectAniBehavior(this);
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

void AnimationState::SetBehaviour(std::string name, bool isReload)
{
	if (isReload)
	{
		behaviour = nullptr;
	}
	else
	{
		behaviourName = name;
		behaviour.reset();
		behaviour = nullptr;
	}

	//behaviour = AnimationFactorys->CreateBehaviour(name);
	behaviour = std::shared_ptr<AniBehavior>(ScriptManager->CreateAniBehavior(behaviourName.c_str()),
		[](AniBehavior* ptr)
		{
			if (ptr)
			{
				ScriptManager->DestroyAniBehavior(ptr);
			}
		}
	);
;

	if(behaviour == nullptr) return;

	behaviour->m_ownerController = this->m_ownerController;
}

void AnimationState::UpdateAnimationSpeed()
{
	ConditionParameter* parameter = m_ownerController->GetOwner()->FindParameter(animationSpeedParameterName);
	if (parameter)
	{
		multiplerAnimationSpeed = parameter->fValue;
	}
}
