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



//using ParameterValue = std::variant<float, int, bool>;
template <typename T>
class TransCondition
{
public:
	TransCondition(T* value, T Comparevalue, conditionType cType) : value(value), Comparevalue(Comparevalue), cType(cType) {};

	bool CheckTrans()
		{
			if (!value)
				return false;

			switch (cType)
			{
			case conditionType::Greater:
				return *value > Comparevalue;
			case conditionType::Less:
				return *value < Comparevalue;
			case conditionType::Equal:
				return *value == Comparevalue;
			case conditionType::NotEqual:
				return *value != Comparevalue;
			default:
				return false;
			}
		}
	//Ÿ�� ,�� ,�Լ�
	conditionType cType = conditionType::Equal;
	//���� ���������
	T* value;
	T Comparevalue;
};

using TransConditionVariant = std::variant<TransCondition<int>, TransCondition<float>, TransCondition<bool>>;
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
	// ���̽ð����� ������ �ð�
	float tranTime =0.f;
	// �ִϸ��̼� Ż�� �ּҽð�
	float exitTime =0.f;

	std::vector<TransConditionVariant> conditions;
};

