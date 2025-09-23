#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "KeyState.h"
#include "PlayerInputComponent.generated.h"
class ActionMap;
class PlayerInputComponent : public Component, public RegistableEvent<PlayerInputComponent>
{
public:
   ReflectPlayerInputComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(PlayerInputComponent)
	

	void Update(float tick) override;
	//쓸 컨트롤러 인덱스 키보드,마우스는 0만지원
	[[Property]]
	int controllerIndex = 0;

	void SetActionMap(std::string mapName);
	void SetActionMap(ActionMap* _actionMap);
	void SetControllerVibration(float tick, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	//쓸 액션맵네임
	ActionMap* m_actionMap = nullptr; 
	[[Property]]
	std::string m_actionMapName = "None";
	//함수가 포함된 스크립트네임 아마 자기가 소유중인것만? 없어도 될듯
	[[Property]]
	std::string m_scriptName{};

	//컴포넌트 생성시 자동으로 인풋액션매니저가 수집 -> 수집된 컴포넌트 순회하면서 등록된맵의 키바인딩된 키가 체크될시 
	//컴포넌트 주인의 스크립트들을 순회하면서 있는 함수 실행시켜줌
};

