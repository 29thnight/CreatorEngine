#include "AnimationState.h"
#include "AniBehavior.h"
#include "AnimationBehviourFatory.h"
#include "AnimationController.h"
#include "Animator.h"
#include "ConditionParameter.h"
#ifndef DYNAMICCPP_EXPORTS
#include "ManagedAniBehavior.h"
#endif
AnimationState::AnimationState()
{
}

AnimationState::~AnimationState()
{
	behaviour.reset();
}

AnimationState::AnimationState(AnimationController* Owner, std::string name) : m_ownerController(Owner), m_name(name)
{
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

#ifndef DYNAMICCPP_EXPORTS
	// C++ 핫리로드 은퇴(9-4) — 애니메이션 상태 스크립트는 C#(ManagedAniBehavior)만 지원한다.
	if (ClrHost::Get().HasAniBehaviour(behaviourName))
	{
		auto managed = std::make_shared<ManagedAniBehavior>(behaviourName);
		if (managed->HasInstance())
		{
			behaviour = managed;
		}
	}
#endif

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
