#include "AnimationController.h"
#include "AnimationState.h"
#include "AniBehaviour.h"
#include "Animator.h"
#include "Skeleton.h"
#include "AvatarMask.h"
void AnimationController::SetNextState(std::string stateName)
{
	
	m_nextState = StateVec[States[stateName]].get();
}

bool AnimationController::BlendingAnimation(float tick)
{
	blendingTime += tick;
	float t = blendingTime / m_curTrans->GetBlendTime();
	m_owner->blendT = std::clamp(t, 0.0f, 1.0f);
	if (blendingTime >= m_curTrans->GetBlendTime()) //블렌드 타임이 끝나면 블렌드종료 -> 다음애니메이션만 계산
	{
		m_owner->m_AnimIndexChosen = m_owner->nextAnimIndex;
		m_AnimationIndex = m_nextAnimationIndex;

		m_timeElapsed = m_nextTimeElapsed;
		m_owner->m_TimeElapsed = m_owner->m_nextTimeElapsed; //*****

		m_owner->nextAnimIndex = -1;
		m_owner->m_isBlend = false;
		m_isBlend = false;
		return false;
	}

	return true;
}

void AnimationController::SetCurState(std::string stateName)
{
	m_curState = StateVec[States[stateName]].get();
	m_curStateName = m_curState->Name;
	
	m_owner->m_AnimIndexChosen = m_curState->AnimationIndex;
	m_AnimationIndex = m_curState->AnimationIndex;
}

std::shared_ptr<AniTransition> AnimationController::CheckTransition()
{
	if (!m_curState)
	{
		//return nullptr;//*****
		m_curState = StateVec[0].get();
		m_curStateName = m_curState->Name;
	}

	if (!m_anyStateVec.empty()) //***** 우선순위 정해두기
	{
		for (auto& state : m_anyStateVec)
		{
			for (auto& trans : state->Transitions)
			{
				if (true == trans->CheckTransiton())
				{
					return trans;
				}
			}
		}
	}


	if (m_curState->Transitions.empty()) return nullptr;
	for (auto& trans : m_curState->Transitions)
	{
		if (true == trans->CheckTransiton())
		{
			return trans;
		}
	}
	return nullptr;
}


void AnimationController::UpdateState()
{

	auto trans = CheckTransition();
	//전이가있으면 애니메이션 블렌딩시작 //블렌딩없는 강제변화있을경우 추가필요*****
	if (nullptr != trans)
	{
		//새전이가있는대 이전 전이가 진행중이었음
		if (m_nextAnimationIndex != -1)
		{
			
			m_owner->m_AnimIndexChosen = m_nextAnimationIndex;
			m_AnimationIndex = m_nextAnimationIndex;
			
			m_owner->nextAnimIndex = -1;
			m_nextAnimationIndex = -1;

			m_timeElapsed = m_nextTimeElapsed;
			m_owner->m_TimeElapsed = m_owner->m_nextTimeElapsed; //*****

			m_owner->m_isBlend = false;
			m_isBlend = false;
		}


		m_nextState = StateVec[States[trans->GetNextState()]].get();
		
		m_owner->nextAnimIndex = m_nextState->AnimationIndex;
		m_nextAnimationIndex = m_nextState->AnimationIndex;

		m_curState->behaviour->Exit();
		m_nextState->behaviour->Enter();
		m_curState = m_nextState;
		m_curStateName = m_curState->Name;
		m_nextState = nullptr;
		m_curTrans = trans.get();
		needBlend = true;
		m_owner->m_isBlend = true;
		m_isBlend = true;

		for (auto& othercontorller : m_owner->m_animationControllers)
		{
			if (othercontorller->name == name) continue;
			if(othercontorller->GetAnimationIndex() == m_nextAnimationIndex)
				m_nextTimeElapsed = othercontorller->m_timeElapsed;
			else
				m_nextTimeElapsed = 0.0f;
		}
		
		//m_owner->m_nextTimeElapsed = 0.0f; //*****

		m_owner->blendT = 0.0f;
		blendingTime = 0.0f;
	}

}
void AnimationController::Update(float tick)
{
	UpdateState();
	if (needBlend)
	{
		if (BlendingAnimation(tick) == false) //true == blending   false  == blend end
			needBlend = false;
	}

	if (m_curState == nullptr) return;
	m_curState->behaviour->Update(tick);
}

int AnimationController::GetAnimatonIndexformState(std::string stateName)
{
	for (auto& state : StateVec)
	{
		if (state->Name == stateName)
			return state->AnimationIndex;
	}
}

AnimationState* AnimationController::CreateState(const std::string& stateName, int animationIndex, bool isAny)
{
	auto it = States.find(stateName);
	if (it != States.end())
	{
		return (StateVec[it->second].get());
	}

	auto state = std::make_shared<AnimationState>(this, stateName);
	if (isAny == true)
		state->m_isAny = true;
	state->AnimationIndex = animationIndex;
	state->SetBehaviour(stateName);
	States.insert(std::make_pair(stateName, StateVec.size()));
	StateVec.push_back(state);
	StateVec.back()->index = StateVec.size() - 1;

	return state.get();
}

AniTransition* AnimationController::CreateTransition(const std::string& curStateName, const std::string& nextStateName)
{
	for (auto& trans : StateVec[States[curStateName]]->Transitions)
	{
		if (trans->GetCurState() == curStateName && trans->GetNextState() == nextStateName)
			return trans.get();

	}
	auto transition = std::make_shared<AniTransition>(curStateName, nextStateName);
	transition->m_ownerController = this;
	StateVec[States[curStateName]]->Transitions.push_back(transition);
	return transition.get();
}


void AnimationController::CreateMask()
{
	m_owner->m_Skeleton->MarkRegionSkeleton();
}

void AnimationController::CheckMask()
{
}
