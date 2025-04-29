#pragma once
#include "Core.Minimal.h"
#include "TransCondition.h"
#include "AniTransition.generated.h"

#include "aniStruct.h"
//using TransConditionVariant = std::variant<TransCondition<int>, TransCondition<float>, TransCondition<bool>>;
class aniFSM;
class AniTransition
{
public:
   ReflectAniTransition
	[[Serializable]]
	AniTransition() = default;
	AniTransition(std::string curStatename, std::string nextStatename);
	~AniTransition();

	void AddCondition(std::string ownerValueName,float Comparevalue, conditionType cType,valueType vType)
	{
		for (const auto& cond : conditions)
		{
			if (cond.CompareParameter.fValue == Comparevalue && cond.valueName == ownerValueName && cond.cType == cType)
			{
				return; // �̹� ������ �ƹ��͵� �� �ϰ� return ���߿� ����� ******
			}
		}
		TransCondition newTrans(Comparevalue,cType,vType);
		newTrans.valueName = ownerValueName;
		newTrans.ownerFSM = owner;
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

	aniFSM* owner{};
private:
	[[Property]]
	std::string curState;
	[[Property]]
	std::string nextState;
	// ���̽ð����� ������ �ð�
	[[Property]]
	float blendTime =0.f;
	// �ִϸ��̼� Ż�� �ּҽð�
	[[Property]]
	float exitTime =0.f;

	//std::vector<TransConditionVariant> conditions;
	
};

