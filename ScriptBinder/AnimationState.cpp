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

	if (behaviourName.empty())
	{
		return;
	}

	//behaviour = AnimationFactorys->CreateBehaviour(name); //WARN : APP Vrifier - Stop #4
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

nlohmann::json AnimationState::Serialize()
{
	nlohmann::json j;
	j["state_name"] = m_name;
	j["behaviourName"] = behaviourName;
	j["animationIndex"] = AnimationIndex;
	j["animationSpeed"] = animationSpeed;
	j["multiplerAnimationSpeed"] = multiplerAnimationSpeed;
	j["animationSpeedParameterName"] = animationSpeedParameterName;
	j["useMultipler"] = (int)useMultipler;
	j["m_isAny"] = m_isAny;
	nlohmann::json transitionsJson = nlohmann::json::array();
	for (auto& trans : Transitions)
	{
		transitionsJson.push_back(trans->Serialize());
	}
	j["transitions"] = transitionsJson;
	return j;
}

AnimationState AnimationState::Deserialize()
{
	return AnimationState();
}
