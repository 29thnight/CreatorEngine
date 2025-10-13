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
	// �����尡 �̹� �����ϴ��� Ȯ��
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

	// GameObject�� AI ������Ʈ ����
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
	// Behavior Tree ��� ���丮 �ʱ�ȭ
	BTNodeFactory->Clear();
	m_btActionNodeNames.clear();
	m_btConditionNodeNames.clear();
	m_btConditionDecoratorNodeNames.clear();
	//InternalAIUpdateEvent.Clear();

	// �Ķ���Ͱ� ���� ��� ���
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

	// InverterNode ���� Decorator ���
	BTNodeFactory->Register("Inverter", []()
	{
		// Decorator�� �ڽ� ��带 ��������, �ڽ� ������ ���丮 ���� �������� ó��
		return std::make_shared<BT::InverterNode>("Inverter", nullptr);
	});

	//��ũ��Ʈȭ �� ��� Ŭ���� �̸� ����
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