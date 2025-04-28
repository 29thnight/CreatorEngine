#pragma once
#include "Component.h"
#include "IUpdatable.h"
#include "Animator.h"
#include "AniTransition.h"
#include "aniFSM.generated.h"
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


	aniState* CurState = nullptr;
	aniState* NextState = nullptr;

	[[Property]]
	std::unordered_map<std::string, std::shared_ptr<aniState>> States;
	[[Property]]
	std::unordered_map<std::string, std::vector<std::shared_ptr<AniTransition>>> Transitions;
	
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
			return dynamic_cast<T*>(it->second.get()); 

		}
		std::shared_ptr<T> state = std::make_shared<T>(this, stateName);
 		States.insert(std::make_pair(stateName, state));

		return state.get();
	}
	AniTransition* CreateTransition(const std::string& curStateName, const std::string& nextStateName)
	{
		auto it = Transitions.find(curStateName);
		if (it != Transitions.end())
		{
			// curStateName에 해당하는 트랜지션 목록을 뒤져서
			for (const auto& transition : it->second)
			{
				if (transition->GetNextState() == nextStateName)
				{
					return transition.get();
				}
			}
		}

		// 없으면 새로 생성
		auto transition = std::make_shared<AniTransition>(curStateName, nextStateName);
		Transitions[curStateName].push_back(transition);

		return transition.get();
	}
private:
	Animator* animator;
	[[Property]]
	bool abc;
	[[Property]]
	std::string curName = "None";
};

