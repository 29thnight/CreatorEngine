#pragma once
//#include "BlackBoard.h"
#include "Core.Minimal.h"
#include "NodeFactory.h"

class AIManager:public Singleton<AIManager>
{
public:
	friend class Singleton;

	AIManager() = default;
	~AIManager() = default;


	BlackBoard& GetGlobalBlackBoard()
	{
		return m_globalBB;
	}

	void Update(float deltaTime)
	{
		//todo : update all AI components or 
		// global AI logic	or
		// global AI state  or
		// global BlackBoard updates or
		// any other AI related logic
	}


	void InitalizeBehaviorTreeSystem()
	{
		// �Ķ���Ͱ� ���� ��� ���
		BTNodeFactory.Register("Sequence", [](const json& params) {
			return std::make_shared<BT::SequenceNode>("Sequence");
				});
		BTNodeFactory.Register("Selector", [](const json& params) {
			return std::make_shared<BT::SelectorNode>("Selector");
			});

		// �Ķ���Ͱ� �ִ� ��� ��� (ActionScriptNode)
		BTNodeFactory.Register("ActionScript", [&](const json& params) {
			// json���� �Ķ���� ����
			std::string name = params.value("name", "ActionScript");
			std::string typeName = params.value("typeName", "");
			std::string methodName = params.value("methodName", "");

			// ����: scriptPtr�� ��Ÿ�ӿ� BehaviorTreeComponent���� �������� �Ҵ��ؾ� ��
			// ���丮 ���������� nullptr�� �ʱ�ȭ
			return std::make_shared<BT::ActionScriptNode>(name, typeName, methodName, nullptr);
			});

		// InverterNode ���� Decorator ���
		BTNodeFactory.Register("Inverter", [](const json& params) {
			// Decorator�� �ڽ� ��带 ��������, �ڽ� ������ ���丮 ���� �������� ó��
			return std::make_shared<BT::InverterNode>("Inverter", nullptr);
			});
	}
	


private:
	

	BlackBoard m_globalBB;
};

static auto& AIManagers = AIManager::GetInstance();