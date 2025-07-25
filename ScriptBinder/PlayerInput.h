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


	
	//�� ��Ʈ�ѷ� �ε��� Ű����,���콺�� 0������
	[[Property]]
	int controllerIndex = 0;

	//�� �׼Ǹʳ���
	[[Property]]
	std::string m_actionMapName{};
	//�Լ��� ���Ե� ��ũ��Ʈ���� �Ƹ� �ڱⰡ �������ΰ͸�? ��� �ɵ�
	[[Property]]
	std::string m_scriptName{};
	//������ �Լ��̸� Attak
	[[Property]]
	std::string m_funName;


	//������Ʈ ������ �ڵ����� ��ǲ�׼ǸŴ����� ���� -> ������ ������Ʈ ��ȸ�ϸ鼭 ��ϵȸ��� Ű���ε��� Ű�� üũ�ɽ� 
	//������Ʈ ������ ��ũ��Ʈ���� ��ȸ�ϸ鼭 �ִ� �Լ� ���������
};

