#pragma once
#include "Component.h"
#include "IUpdatable.h"
#include "Animator.h"
#include "AniTransition.h"
#include "aniStruct.h"
#include "aniFSM.generated.h"
#include "aniState.h"

class aniState;
class AniTransition;


class aniFSM : public Component, public IUpdatable
{
	using TransitionMap = std::unordered_map<std::string, std::vector<std::shared_ptr<AniTransition>>>;
	using TransitionIter = TransitionMap::iterator;
public:
   ReflectaniFSM
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(aniFSM);

	[[Property]]
	aniState* CurState = nullptr;

	aniState* NextState = nullptr;

	std::unordered_map<std::string, /*std::shared_ptr<aniState>*/size_t> States;

	[[Property]]
	std::vector<std::shared_ptr<aniState>> StateVec;

	[[Property]]
	std::vector<aniParameter> Parameters;
	//std::unordered_map<std::string, std::vector<std::shared_ptr<AniTransition>>> Transitions;
	//std::vector<std::shared_ptr<AniTransition>> abvcd;
	//void Update();
	void SetAnimator(Animator* _animator) { animator = _animator; }
	Animator* GetAnimator() { return animator; };
	void SetCurState(std::string stateName);
	void SetNextState(std::string stateName);

	std::shared_ptr<AniTransition> CheckTransition();
	void UpdateState();
	virtual void Update(float tick) override;

	template<typename T>
	T* CreateState(const std::string& stateName)
	{
		
		auto it = States.find(stateName);
		if (it != States.end())
		{
			return dynamic_cast<T*>(StateVec[it->second].get());
		}
		std::shared_ptr<T> state = std::make_shared<T>(this, stateName);
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
		transition->owner = this;
		StateVec[States[curStateName]]->Transitions.push_back(transition);
		return transition.get();
	}

	void AddParameter(const std::string valuename, float value, valueType vType)
	{
		Parameters.push_back(aniParameter(value, vType, valuename));
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
private:
	Animator* animator;
	[[Property]]
	std::string curName = "None";
};

