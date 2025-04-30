#include "AnimationController.h"
#include "aniState.h"
#include "AniBehaviour.h"
void AnimationController::SetNextState(std::string stateName)
{
	
	m_nextState = StateVec[States[stateName]].get();
}

bool AnimationController::BlendingAnimation(float tick)
{
	blendingTime += tick;
	float t = blendingTime / m_curTrans->GetBlendTime();
	m_owner->blendT = std::clamp(t, 0.0f, 1.0f);
	if (blendingTime >= m_curTrans->GetBlendTime()) //���� Ÿ���� ������ �������� -> �����ִϸ��̼Ǹ� ���
	{
		m_owner->m_AnimIndexChosen = m_owner->nextAnimIndex;
		m_owner->m_TimeElapsed = m_owner->m_nextTimeElapsed;
		m_owner->nextAnimIndex = -1;
		m_owner->m_isBlend = false;
		return false;
	}

	return true;
}

void AnimationController::SetCurState(std::string stateName)
{
	m_curState = StateVec[States[stateName]].get();
}

std::shared_ptr<AniTransition> AnimationController::CheckTransition()
{
	if (!m_curState)
	{

		m_curState = StateVec[0].get();
	}
	if (m_curState->Transitions.empty()) return nullptr;
	for (auto& iter : m_curState->Transitions)
	{
		if (true == iter->CheckTransiton())
		{
			return iter;
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
		if (m_owner->nextAnimIndex != -1)
		{
			m_owner->m_AnimIndexChosen = m_owner->nextAnimIndex;
			m_owner->m_TimeElapsed = m_owner->m_nextTimeElapsed;
			m_owner->nextAnimIndex = -1;
			m_owner->m_isBlend = false;

		}


		m_nextState = StateVec[States[trans->GetNextState()]].get();
		m_curState->behaviour->Exit();
		m_nextState->behaviour->Enter();
		m_curState = m_nextState;
		m_nextState = nullptr;
		m_curTrans = trans.get();
		needBlend = true;
		m_owner->m_isBlend = true;
		m_owner->m_nextTimeElapsed = 0.0f;
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
