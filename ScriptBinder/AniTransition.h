#pragma once
#include "Core.Minimal.h"
#include "TransCondition.h"
#include "AniTransition.generated.h"
#include "ConditionParameter.h"

class AnimationState;
class AnimationController;
class AniTransition
{
public:
   ReflectAniTransition
	[[Serializable]]
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
		conditions.push_back(newTrans);
	}

	void AddConditionDefault(std::string ownerValueName, ConditionType cType, ValueType vType)
	{

		TransCondition newTrans(0, cType, vType);
		newTrans.valueName = ownerValueName;
		newTrans.m_ownerController = m_ownerController;
		conditions.push_back(newTrans);
	}
	void SetCurState(std::string curStateName);
	void SetCurState(AnimationState* _curState);
	void SetNextState(std::string nextStateName);
	void SetNextState(AnimationState* _nextStat);
	std::string GetCurState();
	std::string GetNextState();
	bool CheckTransiton();
	float GetBlendTime() { return blendTime; }
	float GetExitTime() { return exitTime; }

	std::vector<TransCondition> GetConditions();
	[[Property]]
	std::vector<TransCondition> conditions{};

	AnimationController* m_ownerController{};
	[[Property]]
	std::string m_name = "NoName";
	[[Property]]
	AnimationState* curState = nullptr;
	[[Property]]
	AnimationState* nextState = nullptr;
private:
	
	//std::string nextState;
	// 전이시간이자 블렌딩될 시간
	[[Property]]
	float blendTime =0.2f;
	// 애니메이션 탈출 최소시간
	[[Property]]
	float exitTime =0.f;

	
	
};

