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
	//�� ��Ʈ�ѷ� �ε��� Ű����,���콺�� 0������
	[[Property]]
	int controllerIndex = 0;

	void SetActionMap(std::string mapName);
	void SetActionMap(ActionMap* _actionMap);
	void SetControllerVibration(float tick, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	//�� �׼Ǹʳ���
	ActionMap* m_actionMap = nullptr; 
	[[Property]]
	std::string m_actionMapName = "None";
	//�Լ��� ���Ե� ��ũ��Ʈ���� �Ƹ� �ڱⰡ �������ΰ͸�? ��� �ɵ�
	[[Property]]
	std::string m_scriptName{};

	//������Ʈ ������ �ڵ����� ��ǲ�׼ǸŴ����� ���� -> ������ ������Ʈ ��ȸ�ϸ鼭 ��ϵȸ��� Ű���ε��� Ű�� üũ�ɽ� 
	//������Ʈ ������ ��ũ��Ʈ���� ��ȸ�ϸ鼭 �ִ� �Լ� ���������
};

