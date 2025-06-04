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

AnimationController::~AnimationController()
{
	if (m_nodeEditor)
		delete m_nodeEditor;
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
	m_curState = FindState(stateName);
	
	m_owner->m_AnimIndexChosen = m_curState->AnimationIndex;
	m_AnimationIndex = m_curState->AnimationIndex;
}

std::shared_ptr<AniTransition> AnimationController::CheckTransition()
{
	if (!m_curState)
	{
		
		m_curState = StateVec[0].get();
		//return nullptr;//*****
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
		
				transitions.erase(
					std::remove_if(transitions.begin(), transitions.end(),
						[&](const std::shared_ptr<AniTransition>& t)
						{
							return t->GetCurState() == stateName || t->GetNextState() == stateName;
						}),
					transitions.end());
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

	transitions.erase(
		std::remove_if(transitions.begin(), transitions.end(),
			[&](const std::shared_ptr<AniTransition>& t)
			{
				return t->GetCurState() == fromStateName && t->GetNextState() == toStateName;
			}),
		transitions.end());
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
	transition->m_name = curStateName + " to " + nextStateName;
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
