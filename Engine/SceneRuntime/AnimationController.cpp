#include "AnimationController.h"
#include "AnimationState.h"
#include "AniBehavior.h"
#include "Animator.h"
#include "Skeleton.h"
#include "AvatarMask.h"
void AnimationController::SetNextState(std::string stateName)
{

	m_nextState = FindState(stateName);
}

AnimationController::~AnimationController()
{
	DeleteAvatarMask();
}

bool AnimationController::BlendingAnimation(float tick)
{
	blendingTime += tick;
	float t = blendingTime / m_curTrans->GetBlendTime();
	m_owner->blendT = std::clamp(t, 0.0f, 1.0f);
	if (blendingTime >= m_curTrans->GetBlendTime()) //������ Ÿ���� ������ ���������� -> �����ִϸ��̼Ǹ� ���
	{
		m_curState = m_nextState;
		m_nextState = nullptr;
		m_owner->m_AnimIndexChosen = m_owner->nextAnimIndex;
		m_AnimationIndex = m_nextAnimationIndex;
		curAnimationProgress = nextAnimationProgress;
		preCurAnimationProgress = preNextAnimationProgress;
		nextAnimationProgress = 0.f;
		preNextAnimationProgress = 0.f;
		m_timeElapsed = m_nextTimeElapsed;
		m_nextTimeElapsed = 0.f;
		m_owner->m_TimeElapsed = m_owner->m_nextTimeElapsed; 

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
#pragma region OLD_CODE
	if (!m_curState)
	{
		if (!StateVec.size() >= 2)   //0���� anystate�����
		{
			if (StateVec[1].get()->m_name != "Ani State")
			{
				m_curState = StateVec[1].get();
			}
			else
			{
				m_curState = StateVec[0].get();
			}
		}
		else
		{
			return nullptr;
		}
	}

	if (m_curState == nullptr) return nullptr;


	AnimationState* aniState = GetAniState().get();
	if (aniState)
	{
		if (!aniState->Transitions.empty())
		{
			for (auto& trans : aniState->Transitions)
			{
				if (trans->hasExitTime)
				{
					if (trans->GetExitTime() >= curAnimationProgress)
						continue;
				}
				if (true == trans->CheckTransiton())
				{
					if (trans->nextState != nullptr && m_curState != trans->nextState)
					{
						return trans;
					}
				}
			}
		}
	}


	AnimationState* transState = m_curState;
	if (m_isBlend)
	{
		transState = m_nextState;
	}
	if (transState->Transitions.empty()) return nullptr;

	
	for (auto& trans : transState->Transitions)
	{
		if (m_isBlend)
		{
			if (true == trans->CheckTransiton(true))
			{
				return trans;
			}
		}
		else
		{
			if (true == trans->CheckTransiton())
			{
				return trans;
			}
		}
	}
	return nullptr;
#pragma endregion
	//if (!m_curState) 
	//{
	//	if (StateVec.size() >= 2) 
	//	{
	//		m_curState = (StateVec[1]->m_name != "Ani State") ? StateVec[1].get() : StateVec[0].get();
	//	}
	//	else 
	//	{
	//		return nullptr;
	//	}
	//}

	//// 1) AnyState ĳ�� ���
	//if (auto aniState = GetAniState()) 
	//{
	//	for (auto& trans : aniState->Transitions) 
	//	{
	//		if (trans->hasExitTime && trans->GetExitTime() >= curAnimationProgress)
	//			continue;

	//		if (trans->CheckTransiton()) 
	//		{
	//			// BUG FIX: �����ݷ� ����
	//			if (trans->nextState != nullptr && m_curState != trans->nextState)
	//				return trans;
	//		}
	//	}
	//}

	//// 2) ���� ������ ������ ���� ������ 1���� ����
	//AnimationState* transState = m_isBlend ? m_nextState : m_curState;
	//if (!transState || transState->Transitions.empty()) return nullptr;

	//const bool checkBlend = m_isBlend;
	//for (auto& trans : transState->Transitions) 
	//{
	//	if (trans->hasExitTime && trans->GetExitTime() >= (checkBlend ? nextAnimationProgress : curAnimationProgress))
	//		continue;

	//	if (trans->CheckTransiton(checkBlend))
	//		return trans;
	//}
	//return nullptr;
}


void AnimationController::UpdateState()
{
	auto trans = CheckTransition();
	/*if (needBlend)
	{
		trans = nullptr;
	}*/
	//���̰������� �ִϸ��̼� ���������� //���������� ������ȭ������� �߰��ʿ�*****
	if (nullptr != trans)
	{
		
		endAnimation = false;
		if (needBlend == true)
		{
			/*if (m_curState->behaviour != nullptr)
				m_curState->behaviour->Exit();
			if (m_nextState->behaviour != nullptr)
				m_nextState->behaviour->Enter();*/
			m_curState = m_nextState;
			m_nextState = nullptr;
			m_owner->m_AnimIndexChosen = m_owner->nextAnimIndex;
			m_AnimationIndex = m_nextAnimationIndex;
			curAnimationProgress = nextAnimationProgress;
			preCurAnimationProgress = preNextAnimationProgress;
			nextAnimationProgress = 0.f;
			preNextAnimationProgress = 0.f;
			m_timeElapsed = m_nextTimeElapsed;
			m_nextTimeElapsed = 0.f;
			m_owner->m_TimeElapsed = m_owner->m_nextTimeElapsed; 

			/*if (m_curState && m_curState->behaviour)
				m_curState->behaviour->Enter();*/
			m_owner->nextAnimIndex = -1;
			m_owner->m_isBlend = false;
			m_isBlend = false;



		}
		m_nextState = FindState(trans->GetNextState());

		if (m_curState->behaviour != nullptr)
			m_curState->behaviour->Exit();
		if (m_nextState->behaviour != nullptr)
			m_nextState->behaviour->Enter();


		m_owner->nextAnimIndex = m_nextState->AnimationIndex;
		m_nextAnimationIndex = m_nextState->AnimationIndex;

		m_curTrans = trans.get();
		needBlend = true;
		m_owner->m_isBlend = true;
		m_isBlend = true;

		if (m_owner->m_animationControllers.size() >= 2)
		{
			for (auto& othercontorller : m_owner->m_animationControllers)
			{
				if (!othercontorller->useController) continue;
				if (othercontorller->name == name) continue;
				if (othercontorller->GetAnimationIndex() == m_nextAnimationIndex)
					m_nextTimeElapsed = othercontorller->m_timeElapsed;
				else
					m_nextTimeElapsed = 0.0f;
			}
		}
		else
		{
			m_nextTimeElapsed = 0.0f;
		}

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

	m_curState->UpdateAnimationSpeed();
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

std::shared_ptr<AnimationState> AnimationController::GetAniState()
{
	for (auto& state : StateVec)
	{
		if (state->m_isAny == true)
			return state;
	}
	return nullptr;
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
	{
		state->m_isAny = true;
		m_anyState = state;
	}
	state->AnimationIndex = animationIndex;
	//state->SetBehaviour(stateName);
	//States.insert(std::make_pair(stateName, StateVec.size()));
	StateVec.push_back(state);
	StateVec.back()->index = StateVec.size() - 1;
	StateNameSet.insert(stateName);
	m_nameToState[stateName] = state;
	return state.get();
}

std::shared_ptr<AnimationState> AnimationController::CreateState_UI()
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
	m_nameToState[uniqueName] = state;
	return state;
}

void AnimationController::DeleteState(std::string stateName)
{
	auto it = std::find_if(StateVec.begin(), StateVec.end(),
		[&](const std::shared_ptr<AnimationState>& state)
		{
			return state->m_name == stateName;
		});

	if (it->get() == m_curState)
	{
		m_curState = nullptr;
	}

	for (auto& state : StateVec)
	{
		auto& transitions = state->Transitions;

		std::erase_if(transitions, [&](const std::shared_ptr<AniTransition>& t)
		{
			return t->GetCurState() == stateName 
				|| t->GetNextState() == stateName;
		});
	}
	if (it != StateVec.end())
	{
		StateVec.erase(it); 
	}
}



void AnimationController::DeleteTransiton(const std::string& fromStateName, const std::string& toStateName)
{
	auto state = FindState(fromStateName);
	if (!state) return;

	auto& transitions = state->Transitions;
	
	std::erase_if(transitions, [&](const std::shared_ptr<AniTransition>& t)
	{
		return t->GetCurState() == fromStateName 
			&& t->GetNextState() == toStateName;
	});
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
	auto curstate = FindState(curStateName);
	if (!curstate) return nullptr;
	for (auto& trans : curstate->Transitions)
	{
		if (trans->GetCurState() == curStateName && trans->GetNextState() == nextStateName)
			return trans.get();
	}
	
	auto nextstate = FindState(nextStateName);
	if (!nextstate) return nullptr;
	auto transition = std::make_shared<AniTransition>();
	transition->SetCurState(curstate);
	transition->SetNextState(nextstate);
	transition->m_ownerController = this;
	transition->m_name = curStateName + " to " + nextStateName;
	curstate->Transitions.push_back(transition);
	return transition.get();
}


void AnimationController::CreateMask()
{
	if (!m_avatarMask)
	{
		useMask = true;
		// I5-D4e-3 — 마스크 생성 창구가 Animator다(experiment 정본·legacy
		// 폴백). region 태깅(MarkRegionSkeleton)은 legacy 폴백 안으로 들어갔다
		// — experiment 경로는 Animator 소유 region 캐시를 쓴다.
		m_avatarMask = new AvatarMask;
		m_avatarMask->RootMask = m_owner->BuildAvatarBoneMasks(*m_avatarMask);
	}
}

void AnimationController::ReCreateMask(AvatarMask* mask)
{
	if (m_avatarMask)
	{
		delete m_avatarMask;
		m_avatarMask = nullptr;
	}
	CreateMask();
	m_avatarMask->ReCreateMask(mask);
}

void AnimationController::DeleteAvatarMask()
{
    if (m_avatarMask)
    {
        useMask = false;
		delete m_avatarMask;
		m_avatarMask = nullptr;
    }
}



void AnimationController::SetUseLayer(bool _useLayer)
{
	m_useLayer = _useLayer;
}
