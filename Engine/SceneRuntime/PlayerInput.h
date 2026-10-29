#pragma once
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "Component.h"
#include "KeyState.h"
class ActionMap;
class [[reflgen::reflect, reflgen::public_methods]] PlayerInputComponent : public meta::identity<PlayerInputComponent, Component>
{
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
	PlayerInputComponent() = default;

	// C3 — 가상 Update 오버라이드를 버리고 비가상 진입점으로. 일시정지 경로
	// (Scene::InternalPauseUpdateForUI)가 이것을 직접 부르므로 이름이 계약이다.
	void TickInput(float tick);
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;

	void SetActionMap(std::string mapName);
	void SetActionMap(ActionMap* _actionMap);
	void SetControllerVibration(float tick, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	void SetControllerVibration(float tick, float power);

	//쓸 액션맵네임
	[[reflgen::ignore]]
	ActionMap* m_actionMap = nullptr;

	std::string m_actionMapName = "None";
	//함수가 포함된 스크립트네임 아마 자기가 소유중인것만? 없어도 될듯
	std::string m_scriptName{};

	//쓸 컨트롤러 인덱스 키보드,마우스는 0만지원
	int controllerIndex = 0;
	//컴포넌트 생성시 자동으로 인풋액션매니저가 수집 -> 수집된 컴포넌트 순회하면서 등록된맵의 키바인딩된 키가 체크될시
	//컴포넌트 주인의 스크립트들을 순회하면서 있는 함수 실행시켜줌
};

