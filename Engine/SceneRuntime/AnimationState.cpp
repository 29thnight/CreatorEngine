#include "AnimationState.h"
#include "AniBehavior.h"
#include "AnimationBehviourFatory.h"
#include "AnimationController.h"
#include "Animator.h"
#include "ConditionParameter.h"
#include "ManagedAniBehavior.h"
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

	// C++ 핫리로드 은퇴(9-4) — 애니메이션 상태 스크립트는 C#(ManagedAniBehavior)만 지원한다.
	if (ClrHost::Get().HasAniBehaviour(behaviourName))
	{
		auto managed = std::make_shared<ManagedAniBehavior>(behaviourName);
		if (managed->HasInstance())
		{
			behaviour = managed;
		}
	}

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
