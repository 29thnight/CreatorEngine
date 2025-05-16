#include "AnimationController.h"
#include "AnimationState.h"
#include "AniBehaviour.h"
#include "Animator.h"
#include "Skeleton.h"
#include "AvatarMask.h"
void AnimationController::SetNextState(std::string stateName)
{
	
	m_nextState = FindState(stateName);
}

bool AnimationController::BlendingAnimation(float tick)
{
	blendingTime += tick;
	float t = blendingTime / m_curTrans->GetBlendTime();
	m_owner->blendT = std::clamp(t, 0.0f, 1.0f);
	if (blendingTime >= m_curTrans->GetBlendTime()) //���� Ÿ���� ������ �������� -> �����ִϸ��̼Ǹ� ���
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
	m_curState = FindState(stateName);
	
	m_owner->m_AnimIndexChosen = m_curState->AnimationIndex;
	m_AnimationIndex = m_curState->AnimationIndex;
}

std::shared_ptr<AniTransition> AnimationController::CheckTransition()
{
	if (!m_curState)
	{
		return nullptr;//*****
		//m_curState = StateVec[0].get();
	}

	if (!m_anyStateVec.empty()) //***** �켱���� ���صα�
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
	//���̰������� �ִϸ��̼� �������� //�������� ������ȭ������� �߰��ʿ�*****
	if (nullptr != trans)
	{
		//�����̰��ִ´� ���� ���̰� �������̾���
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


		m_nextState = FindState(trans->GetNextState());

		m_owner->nextAnimIndex = m_nextState->AnimationIndex;
		m_nextAnimationIndex = m_nextState->AnimationIndex;

		if (m_curState->behaviour != nullptr)
		m_curState->behaviour->Exit();
		if (m_nextState->behaviour != nullptr)
		m_nextState->behaviour->Enter();
		m_curState = m_nextState;
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

	if(m_curState->behaviour != nullptr)
		m_curState->behaviour->Update(tick);
}

int AnimationController::GetAnimatonIndexformState(std::string stateName)
{
	for (auto& state : StateVec)
	{
		if (state->m_name == stateName)
			return state->AnimationIndex;
	}
}

AnimationState* AnimationController::CreateState(const std::string& stateName, int animationIndex, bool isAny)
{
	auto it = FindState(stateName);
	if (it != nullptr)
	{
		return it;
	}

	auto state = std::make_shared<AnimationState>(this, stateName);
	if (isAny == true)
		state->m_isAny = true;
	state->AnimationIndex = animationIndex;
	state->SetBehaviour(stateName);
	States.insert(std::make_pair(stateName, StateVec.size()));
	StateVec.push_back(state);
	StateVec.back()->index = StateVec.size() - 1;
	StateNameSet.insert(stateName);
	return state.get();
}

void AnimationController::CreateState_UI()
{
	std::string uniqueName = "NewState"; 
	std::string baseName = "NewState";
	int count = 0;
	while(StateNameSet.find(uniqueName) !=  StateNameSet.end())
	{
		uniqueName = baseName + " " + std::to_string(count++);
	}

	auto state = std::make_shared<AnimationState>(this, uniqueName);
	StateNameSet.insert(uniqueName);
	StateVec.push_back(state);
	StateVec.back()->index = StateVec.size() - 1;
}

AnimationState* AnimationController::FindState(std::string stateName)
{
	for (auto& state : StateVec)
	{
		if (state->m_name == stateName)
		{
			return state.get();
		}
	}

	return nullptr;
}

AniTransition* AnimationController::CreateTransition(const std::string& curStateName, const std::string& nextStateName)
{
	
	for (auto& trans : FindState(curStateName)->Transitions)
	{
		if (trans->GetCurState() == curStateName && trans->GetNextState() == nextStateName)
			return trans.get();

	}
	auto transition = std::make_shared<AniTransition>(curStateName, nextStateName);
	transition->m_ownerController = this;
	FindState(curStateName)->Transitions.push_back(transition);
	return transition.get();
}


void AnimationController::CreateMask()
{
	m_owner->m_Skeleton->MarkRegionSkeleton();
}

void AnimationController::CheckMask()
{
}
