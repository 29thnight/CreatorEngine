#include "AnimationController.h"
#include "AnimationState.h"
#include "AniBehavior.h"
#include "Animator.h"
#include "BoneRegion.h"
#include "AvatarMask.h"
#include <atomic>
#include <cassert>

namespace
{
    std::atomic<std::uint64_t> g_nextControllerPlaybackId{ 1 };
}

AnimationController::AnimationController()
    : m_playbackId(g_nextControllerPlaybackId.fetch_add(1, std::memory_order_relaxed)) {}

ControllerPlayback& AnimationController::GetPlayback()
{
    assert(m_owner && !weak_from_this().expired());
    return m_owner->GetInstance().GetControllerPlayback(m_playbackId, weak_from_this());
}

const ControllerPlayback& AnimationController::GetPlayback() const
{
    return const_cast<AnimationController*>(this)->GetPlayback();
}

void AnimationController::OnBeforeSerialize()
{
    if (m_owner && !weak_from_this().expired())
        m_curState = GetPlayback().currentState;
}

void AnimationController::SetNextState(std::string stateName)
{

	GetPlayback().nextState = FindState(stateName);
}

AnimationController::~AnimationController()
{
	DeleteAvatarMask();
}

bool AnimationController::BlendingAnimation(float tick)
{
	auto& playback = GetPlayback();
	auto& ownerControl = m_owner->GetPlaybackControl();
	playback.blendingTime += tick;
	float t = playback.blendingTime / playback.currentTransition->GetBlendTime();
	ownerControl.blendT = std::clamp(t, 0.0f, 1.0f);
	if (playback.blendingTime >= playback.currentTransition->GetBlendTime()) //������ Ÿ���� ������ ���������� -> �����ִϸ��̼Ǹ� ���
	{
		playback.currentState = playback.nextState;
		m_curState = playback.currentState;
		playback.nextState = nullptr;
		m_owner->SetSelectedClipIndex(ownerControl.nextClipIndex);
		playback.animationIndex = playback.nextAnimationIndex;
		playback.nextAnimationIndex = -1;
		playback.currentTransition = nullptr;
		playback.currentProgress = playback.nextProgress;
		playback.previousCurrentProgress = playback.previousNextProgress;
		playback.nextProgress = 0.f;
		playback.previousNextProgress = 0.f;
		playback.timeElapsed = playback.nextTimeElapsed;
		playback.nextTimeElapsed = 0.f;
		m_owner->GetInstance().timeElapsed = m_owner->GetInstance().nextTimeElapsed;

		ownerControl.nextClipIndex = -1;
		ownerControl.isBlending = false;
		playback.isBlending = false;
		return false;
	}

	return true;
}

void AnimationController::SetCurState(std::string stateName)
{
	auto& playback = GetPlayback();
	playback.currentState = FindState(stateName);
	m_curState = playback.currentState;

	if (playback.currentState)
	{
		m_owner->SetSelectedClipIndex(playback.currentState->AnimationIndex);
		playback.animationIndex = playback.currentState->AnimationIndex;
	}
}

std::shared_ptr<AniTransition> AnimationController::CheckTransition()
{
	auto& playback = GetPlayback();
	if (!playback.currentState)
	{
		// Select the first playable state, regardless of AnyState name/order.
		for (const auto& state : StateVec)
		{
			if (!state || state->m_isAny || state->AnimationIndex < 0) continue;
			playback.currentState = state.get();
			m_curState = playback.currentState;
			playback.animationIndex = state->AnimationIndex;
			if (m_owner) m_owner->SetSelectedClipIndex(playback.animationIndex);
			break;
		}
	}
	if (!playback.currentState) return nullptr;

	AnimationState* aniState = GetAniState().get();
	if (aniState)
	{
		if (!aniState->Transitions.empty())
		{
			for (auto& trans : aniState->Transitions)
			{
				if (trans->hasExitTime)
				{
					if (trans->GetExitTime() >= GetPlayback().currentProgress)
						continue;
				}
				if (true == trans->CheckTransiton())
				{
					if (trans->nextState != nullptr && playback.currentState != trans->nextState)
					{
						return trans;
					}
				}
			}
		}
	}


	AnimationState* transState = playback.currentState;
	if (playback.isBlending)
	{
		transState = playback.nextState;
	}
	if (!transState || transState->Transitions.empty()) return nullptr;

	
	for (auto& trans : transState->Transitions)
	{
		if (playback.isBlending)
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
		auto& playback = GetPlayback();
		auto& ownerControl = m_owner->GetPlaybackControl();
		
		playback.endAnimation = false;
		if (playback.needBlend == true)
		{
			/*if (m_curState->behaviour != nullptr)
				m_curState->behaviour->Exit();
			if (m_nextState->behaviour != nullptr)
				m_nextState->behaviour->Enter();*/
			playback.currentState = playback.nextState;
			m_curState = playback.currentState;
			playback.nextState = nullptr;
			m_owner->SetSelectedClipIndex(ownerControl.nextClipIndex);
			playback.animationIndex = playback.nextAnimationIndex;
			playback.nextAnimationIndex = -1;
			playback.currentTransition = nullptr;
			playback.currentProgress = playback.nextProgress;
			playback.previousCurrentProgress = playback.previousNextProgress;
			playback.nextProgress = 0.f;
			playback.previousNextProgress = 0.f;
			playback.timeElapsed = playback.nextTimeElapsed;
			playback.nextTimeElapsed = 0.f;
			m_owner->GetInstance().timeElapsed = m_owner->GetInstance().nextTimeElapsed;

			/*if (m_curState && m_curState->behaviour)
				m_curState->behaviour->Enter();*/
			ownerControl.nextClipIndex = -1;
			ownerControl.isBlending = false;
			playback.isBlending = false;



		}
		playback.nextState = FindState(trans->GetNextState());

		if (playback.currentState->behaviour != nullptr)
			playback.currentState->behaviour->Exit();
		if (playback.nextState->behaviour != nullptr)
			playback.nextState->behaviour->Enter();


		ownerControl.nextClipIndex = playback.nextState->AnimationIndex;
		playback.nextAnimationIndex = playback.nextState->AnimationIndex;

		playback.currentTransition = trans.get();
		playback.needBlend = true;
		ownerControl.isBlending = true;
		playback.isBlending = true;

		if (m_owner->m_animationControllers.size() >= 2)
		{
			for (auto& othercontorller : m_owner->m_animationControllers)
			{
				if (!othercontorller->useController) continue;
				if (othercontorller->name == name) continue;
				if (othercontorller->GetAnimationIndex() == playback.nextAnimationIndex)
					playback.nextTimeElapsed = othercontorller->GetPlayback().timeElapsed;
				else
					playback.nextTimeElapsed = 0.0f;
			}
		}
		else
		{
			playback.nextTimeElapsed = 0.0f;
		}

		ownerControl.blendT = 0.0f;
		playback.blendingTime = 0.0f;
	}

}
void AnimationController::Update(float tick)
{
	auto& playback = GetPlayback();
	UpdateState();
	if (playback.needBlend)
	{
		if (BlendingAnimation(tick) == false) //true == blending   false  == blend end
			playback.needBlend = false;
	}

	if (playback.currentState == nullptr) return;

	playback.currentState->UpdateAnimationSpeed();
	if(playback.currentState->behaviour != nullptr)
		playback.currentState->behaviour->Update(tick);
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
		if (state && state->m_isAny == true)
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

	if (it == StateVec.end()) return;
	auto& playback = GetPlayback();
	if (it->get() == playback.currentState)
	{
		playback.currentState = nullptr;
		m_curState = nullptr;
	}
	if (it->get() == playback.nextState)
	{
		playback.nextState = nullptr;
		playback.currentTransition = nullptr;
		playback.nextAnimationIndex = -1;
		playback.needBlend = false;
		playback.isBlending = false;
		m_owner->GetPlaybackControl().nextClipIndex = -1;
		m_owner->GetPlaybackControl().isBlending = false;
	}
	else if (playback.currentTransition
		&& (playback.currentTransition->curState == it->get()
			|| playback.currentTransition->nextState == it->get()))
	{
		playback.currentTransition = nullptr;
		playback.nextState = nullptr;
		playback.nextAnimationIndex = -1;
		playback.needBlend = false;
		playback.isBlending = false;
		m_owner->GetPlaybackControl().nextClipIndex = -1;
		m_owner->GetPlaybackControl().isBlending = false;
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
	StateVec.erase(it);
}



void AnimationController::DeleteTransiton(const std::string& fromStateName, const std::string& toStateName)
{
	auto state = FindState(fromStateName);
	if (!state) return;

	auto& transitions = state->Transitions;
	auto& playback = GetPlayback();
	
	std::erase_if(transitions, [&](const std::shared_ptr<AniTransition>& t)
	{
		const bool remove = t->GetCurState() == fromStateName
			&& t->GetNextState() == toStateName;
		if (remove && playback.currentTransition == t.get())
		{
			playback.currentTransition = nullptr;
			playback.nextState = nullptr;
			playback.nextAnimationIndex = -1;
			playback.needBlend = false;
			playback.isBlending = false;
			m_owner->GetPlaybackControl().nextClipIndex = -1;
			m_owner->GetPlaybackControl().isBlending = false;
		}
		return remove;
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
		m_avatarMask->isHumanoid = false; // new masks author weights by bone name
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
