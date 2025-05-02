#pragma once
#include "Component.h"
#include "IUpdatable.h"
#include "Animator.h"
#include "AniTransition.h"

class aniState;
class AniTransition;
class aniFSM : public Component, public IUpdatable
{
	using TransitionMap = std::unordered_map<std::string, std::vector<std::shared_ptr<AniTransition>>>;
	using TransitionIter = TransitionMap::iterator;
public:
	GENERATED_BODY(aniFSM);
	aniState* CurState = nullptr;
	aniState* NextState = nullptr;

	std::unordered_map<std::string, std::shared_ptr<aniState>> States;
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
		std::shared_ptr<T> state = std::make_shared<T>(this, stateName);
		States.insert(std::make_pair(stateName, state));

		return state.get();
	}
	AniTransition* CreateTransition(const std::string& curStateName,const std::string& nextStateName)
	{
		auto transition = std::make_shared<AniTransition>(curStateName,nextStateName);
		Transitions[curStateName].push_back(transition);

		return transition.get();
	}
private:
	Animator* animator;
};

