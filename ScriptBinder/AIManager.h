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
		// 파라미터가 없는 노드 등록
		BTNodeFactory.Register("Sequence", [](const json& params) {
			return std::make_shared<BT::SequenceNode>("Sequence");
				});
		BTNodeFactory.Register("Selector", [](const json& params) {
			return std::make_shared<BT::SelectorNode>("Selector");
			});

		// 파라미터가 있는 노드 등록 (ActionScriptNode)
		BTNodeFactory.Register("ActionScript", [&](const json& params) {
			// json에서 파라미터 추출
			std::string name = params.value("name", "ActionScript");
			std::string typeName = params.value("typeName", "");
			std::string methodName = params.value("methodName", "");

			// 주의: scriptPtr은 런타임에 BehaviorTreeComponent에서 동적으로 할당해야 함
			// 팩토리 시점에서는 nullptr로 초기화
			return std::make_shared<BT::ActionScriptNode>(name, typeName, methodName, nullptr);
			});

		// InverterNode 같은 Decorator 등록
		BTNodeFactory.Register("Inverter", [](const json& params) {
			// Decorator는 자식 노드를 가지지만, 자식 연결은 팩토리 이후 로직에서 처리
			return std::make_shared<BT::InverterNode>("Inverter", nullptr);
			});
	}
	


private:
	

	BlackBoard m_globalBB;
};

static auto& AIManagers = AIManager::GetInstance();