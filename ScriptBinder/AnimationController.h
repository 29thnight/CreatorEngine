#pragma once
#include "Component.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AnimationController.generated.h"

class aniState;
class AniTransition;
class AvatarMask;
class Animator;
class AnimationController
{
public:
   ReflectAnimationController
	[[Serializable]]
    AnimationController() = default;

    [[Property]]
    std::string name;
	[[Property]]
	AnimationState* m_curState = nullptr;

	AnimationState* m_nextState = nullptr;

	bool needBlend =false;
	std::unordered_map<std::string, size_t> States;

	[[Property]]
	std::vector<std::shared_ptr<AnimationState>> StateVec;

	bool BlendingAnimation(float tick);
	//void SetAnimator(Animator* _animator) { animator = _animator; }
	Animator* GetOwner() { return m_owner; };
	void SetCurState(std::string stateName);
	void SetNextState(std::string stateName);

	
	std::shared_ptr<AniTransition> CheckTransition();
	void UpdateState();
	void Update(float tick);
	int GetAnimatonIndexformState(std::string stateName)
	{
		for (auto& state : StateVec)
		{
			if (state->Name == stateName)
			{
				return state->AnimationIndex;
			}
		}
		
	}
	int GetNextAnimationIndex() 
	{
		
		return 1;
	}

	AnimationState* CreateState(const std::string& stateName, int animationIndex)
	{
		auto it = States.find(stateName);
		if (it != States.end())
		{
			return (StateVec[it->second].get());
		}
		auto state = std::make_shared<AnimationState>(this, stateName);
		state->AnimationIndex = animationIndex;
		state->SetBehaviour(stateName);
		States.insert(std::make_pair(stateName, StateVec.size()));
		StateVec.push_back(state);
		StateVec.back()->index = StateVec.size() - 1;
		return state.get();
	}

	AniTransition* CreateTransition(const std::string& curStateName, const std::string& nextStateName)
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

	
	void CreateMask();
	Animator* m_owner{};

	//void AddLayer(AnimationController* layer) { m_ControllerLayers.push_back(layer); };
private:
	float blendingTime = 0;
	//지금일어나는중인 전이 - 블렌드시간 탈출시간등 알려고
	AniTransition* m_curTrans{};


	AvatarMask* m_avatarMask{};
};

