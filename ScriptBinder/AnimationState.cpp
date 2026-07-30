#include "AnimationState.h"
#include "AniBehavior.h"
#include "HotLoadSystem.h"
#include "AnimationBehviourFatory.h"
#include "AnimationController.h"
#include "Animator.h"
#include "ConditionParameter.h"
#ifndef DYNAMICCPP_EXPORTS
#include "ManagedAniBehavior.h"
#endif
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

#ifndef DYNAMICCPP_EXPORTS
	// C++ 팩토리에 없는 이름이면 C# 쪽을 찾아본다.
	//
	// 순서를 이렇게 둔 이유: 기존 C++ 애니메이션 스크립트의 동작을 조금도 바꾸지 않기
	// 위해서다. 이름이 겹치면 지금까지처럼 C++이 이긴다.
	if (nullptr == behaviour && ClrHost::Get().HasAniBehaviour(behaviourName))
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
