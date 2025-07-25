#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "PlayerInput.generated.h"
class PlayerInput : public Component
{
public:
   ReflectPlayerInput
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(PlayerInput)


	
	//쓸 컨트롤러 인덱스 키보드,마우스는 0만지원
	[[Property]]
	int controllerIndex = 0;

	//쓸 액션맵네임
	[[Property]]
	std::string m_actionMapName{};
	//함수가 포함된 스크립트네임 아마 자기가 소유중인것만? 없어도 될듯
	[[Property]]
	std::string m_scriptName{};
	//실행함 함수이름 Attak
	[[Property]]
	std::string m_funName;


	//컴포넌트 생성시 자동으로 인풋액션매니저가 수집 -> 수집된 컴포넌트 순회하면서 등록된맵의 키바인딩된 키가 체크될시 
	//컴포넌트 주인의 스크립트들을 순회하면서 있는 함수 실행시켜줌
};

