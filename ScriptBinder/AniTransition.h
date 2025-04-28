#pragma once
#include "Core.Minimal.h"
#include <variant>

enum class conditionType
{
	Greater,
	Less,
	Equal,
	NotEqual,
};
AUTO_REGISTER_ENUM(conditionType)

enum class valueType
{
	Float,
	Int,
	Bool,
};
AUTO_REGISTER_ENUM(valueType)
//using ParameterValue = std::variant<float, int, bool>;
//template <typename T>
class TransCondition
{
public:
	TransCondition(float* value, float Comparevalue, conditionType cType) : FValue(value), FCompareValue(Comparevalue), cType(cType) { vType = valueType::Float; };
	TransCondition(int* value, int Comparevalue, conditionType cType) : IValue(value), ICompareValue(Comparevalue), cType(cType) { vType = valueType::Int; };
	TransCondition(bool* value, bool Comparevalue, conditionType cType) : BValue(value), BCompareValue(Comparevalue), cType(cType) { vType = valueType::Bool; };
	bool CheckTrans();
	//타입 ,값 ,함수
	conditionType cType = conditionType::Equal;
	valueType vType = valueType::Float;
	//내가 들고있을값
	//T* value;
	//T Comparevalue;

	int* IValue;
	int ICompareValue;

	float* FValue;
	float FCompareValue;

	bool* BValue;
	bool BCompareValue;
		
};

//using TransConditionVariant = std::variant<TransCondition<int>, TransCondition<float>, TransCondition<bool>>;
class AniTransition
{
public:
	AniTransition(std::string curStatename, std::string nextStatename);
	~AniTransition();

	template <typename T>
	void AddCondition(T* value, T Comparevalue, conditionType cType)
	{
			conditions.push_back(TransCondition(value, Comparevalue, cType));
	}
	void SetCurState(std::string curStatename) {curState = curStatename;}
	void SetNextState(std::string nextStatename) { nextState = nextStatename; }
	std::string GetCurState()const { return curState; }
	std::string GetNextState()const { return nextState; }
	bool CheckTransiton();

private:
	std::string curState;
	std::string nextState;
	// 전이시간이자 블렌딩될 시간
	float tranTime =0.f;
	// 애니메이션 탈출 최소시간
	float exitTime =0.f;

	//std::vector<TransConditionVariant> conditions;
	std::vector<TransCondition> conditions;
};

