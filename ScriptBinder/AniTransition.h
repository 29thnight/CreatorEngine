#pragma once
#include "Core.Minimal.h"
#include "TransCondition.h"
#include "AniTransition.generated.h"
#include "ConditionParameter.h"


class AnimationController;
class AniTransition
{
public:
   ReflectAniTransition
	[[Serializable]]
	AniTransition() = default;
	AniTransition(std::string curStatename, std::string nextStatename);
	~AniTransition();

	template<typename T>
	void AddCondition(std::string ownerValueName,T Comparevalue, ConditionType cType,ValueType vType)
	{

		TransCondition newTrans(Comparevalue,cType,vType);
		newTrans.valueName = ownerValueName;
		newTrans.m_ownerController = m_ownerController;
		conditions.push_back(newTrans);
	}
	void SetCurState(std::string curStatename) {curState = curStatename;}
	void SetNextState(std::string nextStatename) { nextState = nextStatename; }
	std::string GetCurState()const { return curState; }
	std::string GetNextState()const { return nextState; }
	bool CheckTransiton();
	float GetBlendTime() { return blendTime; }
	float GetExitTime() { return exitTime; }

	[[Property]]
	std::vector<TransCondition> conditions;

	AnimationController* m_ownerController{};
private:
	[[Property]]
	std::string curState;
	[[Property]]
	std::string nextState;
	// ���̽ð����� ������ �ð�
	[[Property]]
	float blendTime =0.2f;
	// �ִϸ��̼� Ż�� �ּҽð�
	[[Property]]
	float exitTime =0.f;

	//std::vector<TransConditionVariant> conditions;
	
};

