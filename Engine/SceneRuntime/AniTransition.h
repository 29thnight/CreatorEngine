#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "TransCondition.h"
#include "ConditionParameter.h"
class AnimationState;
class AnimationController;
class [[reflgen::reflect]] AniTransition
{
   public:
public:
	AniTransition() = default;
	//AniTransition(std::string curStatename, std::string nextStatename, AnimationController* owner);
	AniTransition(AnimationState* _curState, AnimationState* _nextState);
	~AniTransition();

	template<typename T>
	void AddCondition(std::string ownerValueName,T Comparevalue, ConditionType cType,ValueType vType)
	{

		TransCondition newTrans(Comparevalue,cType,vType);
		newTrans.valueName = ownerValueName;
		newTrans.m_ownerController = m_ownerController;
		newTrans.SetValue(ownerValueName);
		conditions.push_back(newTrans);
	}

	TransCondition* AddConditionDefault(std::string ownerValueName, ConditionType cType, ValueType vType)
	{
		TransCondition newTrans(0, cType, vType);
		newTrans.valueName = ownerValueName;
		newTrans.m_ownerController = m_ownerController;
		newTrans.SetValue(ownerValueName);
		newTrans.SetCondition(ownerValueName);
		conditions.push_back(newTrans);

		return &conditions.back();
	}
	void DeleteCondition(int _index);
	void SetCurState(std::string _curStateName);
	void SetCurState(AnimationState* _curState);
	void SetNextState(std::string _nextStateName);
	void SetNextState(AnimationState* _nextStat);
	std::string GetCurState();
	std::string GetNextState();
	bool CheckTransiton(bool isBlend = false);
	float GetBlendTime() { return blendTime; }
	float GetExitTime() { return exitTime; }
	std::vector<TransCondition> GetConditions();

public:
	std::vector<TransCondition> conditions{};

	[[reflgen::ignore]]
	AnimationController* m_ownerController{};

	std::string m_name = "NoName";

	[[reflgen::ignore]]
	AnimationState* curState = nullptr;

	[[reflgen::ignore]]
	AnimationState* nextState = nullptr;

	std::string curStateName{};
	std::string nextStateName{};
	float exitTime = 0.1f;
	float blendTime = 0.2f;
	bool hasExitTime = false;
};

