#pragma once
#include "Core.Minimal.h"



class TransCondition
{
public:
	//타입 ,값 ,함수


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
	// 전이시간이자 블렌딩될 시간
	float tranTime =0.f;
	// 애니메이션 탈출 최소시간
	float exitTime =0.f;
};

