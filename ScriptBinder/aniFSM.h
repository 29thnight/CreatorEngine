#pragma once
#include "Component.h"

class aniState;

class aniFSM : public Component, public IUpdatable
{
	
public:
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(aniFSM);
	aniState* CurState = nullptr;
	aniState* NextState = nullptr;

	std::unordered_map<std::string, std::shared_ptr<aniState>> States;

	//void Update();
	void SetNextState(std::string stateName);
	virtual void Update(float tick) override;
	template<typename T>
	T* CreateState(const std::string& stateName)
	{
		std::shared_ptr<T> state = std::make_shared<T>(this, stateName);
		States.insert(std::make_pair(stateName, state));

		return state.get();
	}
};

