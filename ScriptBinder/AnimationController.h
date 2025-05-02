#pragma once
#include "Component.h"
#include "IUpdatable.h"
#include "Animator.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AnimationController.generated.h"

class aniState;
class AniTransition;


class AnimationController : public IUpdatable
{
	/*using TransitionMap = std::unordered_map<std::string, std::vector<std::shared_ptr<AniTransition>>>;
	using TransitionIter = TransitionMap::iterator;*/
public:
   ReflectAnimationController
	[[Serializable]]
   AnimationController() = default;

	[[Property]]
	AnimationState* m_curState = nullptr;

	AnimationState* m_nextState = nullptr;

	bool needBlend =false;
	std::unordered_map<std::string, size_t> States;

	[[Property]]
	std::vector<std::shared_ptr<AnimationState>> StateVec;

	[[Property]]
	std::vector<ConditionParameter> Parameters;

	bool BlendingAnimation(float tick);
	//void SetAnimator(Animator* _animator) { animator = _animator; }
	Animator* GetOwner() { return m_owner; };
	void SetCurState(std::string stateName);
	void SetNextState(std::string stateName);

	std::shared_ptr<AniTransition> CheckTransition();
	void UpdateState();
	virtual void Update(float tick) override;


	AnimationState* CreateState(const std::string& stateName)
	{
		auto it = States.find(stateName);
		if (it != States.end())
		{
			return (StateVec[it->second].get());
		}
		auto state = std::make_shared<AnimationState>(this, stateName);
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

	template<typename T>
	void AddParameter(const std::string valuename, T value, ValueType vType)
	{
		for (auto& parm : Parameters)
		{
			if (parm.name == valuename)
				return;
		}
		Parameters.push_back(ConditionParameter(value, vType, valuename));
	}

	template<typename T>
	void SetParameter(const std::string valuename,T Value)
	{
		for (auto& param : Parameters)
		{
			if (param.name == valuename)
			{
				param.UpdateParameter(Value);
			}
		}
	}
	
	Animator* m_owner{};
private:
	//Animator* animator;
	float blendingTime =0;
	//지금일어나는중인 전이 - 블렌드시간 탈출시간등 알려고
	AniTransition* m_curTrans{};

	//state layer //상체 하체 등 나누기용
};

