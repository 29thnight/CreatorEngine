#pragma once
#include "Core.Minimal.h"



class TransCondition
{
public:
	//Ÿ�� ,�� ,�Լ�


};



class AniTransition
{
public:
	AniTransition();
	~AniTransition();


	std::string GetCurState()const { return curState; }
	std::string GetCurState()const { return nextState; }
	//void CheckTransiton();

private:
	std::string curState;
	std::string nextState;
	// ���̽ð����� ������ �ð�
	float tranTime =0.f;
	// �ִϸ��̼� Ż�� �ּҽð�
	float exitTime =0.f;
};

