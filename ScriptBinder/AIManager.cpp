#ifndef DYNAMICCPP_EXPORTS
#include "AIManager.h"
#include "BehaviorTreeComponent.h"
#include "StateMachineComponent.h"
#include "SceneManager.h"
#include "MeshRenderer.h"
#include "Camera.h"
//#include <execution>

BlackBoard* AIManager::CreateBlackBoard(const std::string& aiName)
{
	// 블랙보드가 이미 존재하는지 확인
	if (m_blackBoardFind.find(aiName) != m_blackBoardFind.end())
		return m_blackBoardFind[aiName];

	m_blackBoards.emplace_back();
	m_blackBoardFind.emplace(aiName, &m_blackBoards.back());

	return &m_blackBoards.back();
}

void AIManager::RemoveBlackBoard(const std::string& aiName)
{
	auto it = m_blackBoardFind.find(aiName);
	if (it != m_blackBoardFind.end())
	{
		std::erase_if(m_blackBoards, [&](const BlackBoard& bb) { return &bb == it->second; });
		m_blackBoardFind.erase(it);
	}
}

void AIManager::InternalAIUpdate(float deltaSeconds)
{
	std::vector<IAIComponent*> compVec{};
	auto camera = CameraManagement->GetLastCamera();
	if (!camera) return;

	for (auto& [obj, comp] : m_aiComponentMap)
	{
		if (!obj || !comp) continue;

		if (obj->m_ownerScene != SceneManagers->GetActiveScene()) continue;

		DirectX::BoundingBox objBox{};
		objBox.Extents = { 3.f, 3.f, 3.f };

		auto meshComp = obj->GetComponent<MeshRenderer>();
		if (meshComp)
		{
			objBox = meshComp->GetBoundingBox();
		}
		else
		{
			DirectX::BoundingBox localObjBox{ objBox };
			auto mat = obj->m_transform.GetWorldMatrix();
			localObjBox.Transform(objBox, mat);
		}

		auto frustum = camera->GetFrustum();
		if (frustum.Intersects(objBox))
		{
			compVec.push_back(comp);
		}
	}

	auto updateFunc = [deltaSeconds](IAIComponent* comp)
	{
		try
		{
			comp->InternalAIUpdate(deltaSeconds);
		}
		catch (const std::exception& e)
		{
			std::cerr << "InternalAIUpdate Exception : " << e.what() << std::endl;
		}
	};

	std::ranges::for_each(compVec.begin(), compVec.end(), updateFunc);
}

void AIManager::RegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	// GameObject와 AI 컴포넌트 매핑
	m_aiComponentMap[gameObject] = aiComponent;
}

void AIManager::UnRegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	auto it = m_aiComponentMap.find(gameObject);
	if (it != m_aiComponentMap.end() && it->second == aiComponent)
	{
		m_aiComponentMap.erase(it);
	}
}

BT::BTNode::NodePtr AIManager::CreateNode(std::string_view nodeName)
{
	return BTNodeFactory->Create(nodeName.data());
}

void AIManager::ClearTreeInAIComponent()
{
	for (auto& [gameObject, aiComponent] : m_aiComponentMap)
	{
		if (aiComponent->GetAIType() == AIType::BT)
		{
			BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
			ptr->ClearTree();
		}
	}
}

void AIManager::InitalizeBehaviorTreeSystem()
{
	// Behavior Tree 노드 팩토리 초기화
	BTNodeFactory->Clear();
	m_btActionNodeNames.clear();
	m_btConditionNodeNames.clear();
	m_btConditionDecoratorNodeNames.clear();
	//InternalAIUpdateEvent.Clear();

	// 파라미터가 없는 노드 등록
	BTNodeFactory->Register("RootSequence", []()
	{
		return std::make_shared<BT::SequenceNode>("RootSequence");
	});

	BTNodeFactory->Register("Sequence", []()
	{
		return std::make_shared<BT::SequenceNode>("Sequence");
	});

	BTNodeFactory->Register("Selector", []()
	{
		return std::make_shared<BT::SelectorNode>("Selector");
	});

	BTNodeFactory->Register("WeightedSelector", []()
	{
		return std::make_shared<BT::WeightedSelectorNode>("WeightedSelector");
	});

	// InverterNode 같은 Decorator 등록
	BTNodeFactory->Register("Inverter", []()
	{
		// Decorator는 자식 노드를 가지지만, 자식 연결은 팩토리 이후 로직에서 처리
		return std::make_shared<BT::InverterNode>("Inverter", nullptr);
	});

	//스크립트화 된 노드 클래스 이름 추출
	int actionSize = 0;
	auto nameArr = ScriptManager->ListBTActionNodeNames(&actionSize);

	for (int i = 0; i < actionSize; ++i)
	{
		const std::string& actionName = nameArr[i];
		m_btActionNodeNames.push_back(actionName);
	}

	int conditionSize = 0;
	auto conditionArr = ScriptManager->ListBTConditionNodeNames(&conditionSize);

	for (int i = 0; i < conditionSize; ++i)
	{
		const std::string& conditionName = conditionArr[i];
		m_btConditionNodeNames.push_back(conditionName);
	}

	int conditionDecoratorSize = 0;
	auto conditionDecoratorArr = ScriptManager->ListBTConditionDecoratorNodeNames(&conditionDecoratorSize);
	for (int i = 0; i < conditionDecoratorSize; ++i)
	{
		const std::string& conditionDecoratorName = conditionDecoratorArr[i];
		m_btConditionDecoratorNodeNames.push_back(conditionDecoratorName);
	}

	// Register the script-based nodes
	for (const auto& actionName : m_btActionNodeNames)
	{
		BTNodeFactory->Register(actionName, [actionName]()
		{
			return std::shared_ptr<BT::ActionNode>(
				ScriptManager->CreateActionNode(actionName.c_str()),
				[](BT::ActionNode* ptr)
				{
					ScriptManager->DestroyActionNode(ptr);
				});
		});
	}

	for (const auto& conditionName : m_btConditionNodeNames)
	{
		BTNodeFactory->Register(conditionName, [conditionName]()
		{
			return std::shared_ptr<BT::ConditionNode>(
				ScriptManager->CreateConditionNode(conditionName.c_str()),
				[](BT::ConditionNode* ptr)
				{
					ScriptManager->DestroyConditionNode(ptr);
				});
		});
	}

	for (const auto& conditionDecoratorName : m_btConditionDecoratorNodeNames)
	{
		BTNodeFactory->Register(conditionDecoratorName, [conditionDecoratorName]()
		{
			return std::shared_ptr<BT::ConditionDecoratorNode>(
				ScriptManager->CreateConditionDecoratorNode(conditionDecoratorName.c_str()),
				[](BT::ConditionDecoratorNode* ptr)
				{
					ScriptManager->DestroyConditionDecoratorNode(ptr);
				});
		});
	}


	for (auto& [gameObject, aiComponent] : m_aiComponentMap)
	{
		if (aiComponent->GetAIType() == AIType::BT)
		{
			BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
			ptr->GraphToBuild();
		}
	}

}

#endif // DYNAMICCPP_EXPORTS