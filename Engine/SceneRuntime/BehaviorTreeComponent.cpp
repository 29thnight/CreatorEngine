#include "BehaviorTreeComponent.h"
#include "DataSystem.h"
#include "BTGraphFlatten.h"
#include "BTBuildGraphAuthoring.h"
#include "ClrHost.h"
#include "SceneManager.h"
#include "AuthoringParsedDocument.h"

void BehaviorTreeComponent::Initialize()
{
	if (m_BlackBoardGuid != nullFileGuid)
	{
		//TODO: 블랙보드 인스턴스 생성 준비
		m_pBlackboard = new BlackBoard();

		file::path blackBoardPath = DataSystems->GetFilePath(m_BlackBoardGuid);
		if (!file::exists(blackBoardPath))
		{
			Debug->LogError("Blackboard File not Exists");
		}
		else
		{
			std::string _blackBoardName = blackBoardPath.stem().string();
			m_pBlackboard->Deserialize(_blackBoardName);
		}

	}
	else
	{
		Debug->LogError("Blackboard GUID is invalid.");
	}

	if (m_BehaviorTreeGuid != nullFileGuid)
	{
		GraphToBuild();
	}
	else
	{
		Debug->LogError("Behavior Tree GUID is invalid.");
	}
}

void BehaviorTreeComponent::OnInitialized()
{
	Initialize();
}

void BehaviorTreeComponent::OnAddedToScene()
{
	AIManagers->RegisterAIComponent(GetOwner(), this);
}

void BehaviorTreeComponent::OnRemovingFromScene()
{
	AIManagers->UnRegisterAIComponent(GetOwner(), this);
}

void BehaviorTreeComponent::InternalAIUpdate(float deltaSecond)
{
	if (GetOwner()->IsEnabled() == false) return;
	if (!SceneManagers->m_isGameStart) return;
	if (m_treeInstanceId < 0) return;

	// 여기서 트리를 돌지 않는다. 큐에 담기만 하고 실행은 틱 경계의 FlushAITicks가 한다.
	//
	// 이 함수는 게임 스레드가 아니라 AI 잡 스레드에서 불린다(Scene의 m_AIFuture).
	// 관리 코드는 게임 스레드에서만 부를 수 있으므로(GC 규약) 여기서 바로 넘길 수 없다.
	// 물리·애니메이션 이벤트가 같은 이유로 같은 규약을 쓴다.
	ClrHost::Get().QueueAITick(m_treeInstanceId, deltaSecond);
}

void BehaviorTreeComponent::OnUninitializing()
{
	if (m_pBlackboard)
	{
		/*m_pBlackboard = nullptr;*/
		Memory::SafeDelete(m_pBlackboard);
		AIManagers->RemoveBlackBoard(name);
	}
	// 관리 측 트리도 함께 내린다. 남기면 Running 상태가 다음 씬으로 샌다.
	if (m_treeInstanceId >= 0)
	{
		ClrHost::Get().DestroyBehaviorTree(m_treeInstanceId);
		m_treeInstanceId = -1;
	}
}

// 네이티브 트리 조립기(BuildTree·BuildTreeRecursively)가 여기 있었다.
// PHASE 9-8 B4에서 제거했다 — 조립은 이제 관리 측 BTGraphBuilder가 한다.
//
// 둘을 함께 두지 않은 이유: 조립기가 둘이면 어느 쪽이 실제로 도는지가 코드에서
// 드러나지 않고, 한쪽만 고친 채로 다른 쪽이 도는 상황이 생긴다. 저작 데이터의
// 형식은 그대로이므로 기존 BT 에셋은 수정 없이 열린다.

void BehaviorTreeComponent::SendGraphToManaged(const BTBuildGraph& graph)
{
	// 이전 트리가 있으면 먼저 내린다. 그래프를 다시 읽는 경로(에디터 편집 후 재생)에서
	// 남겨 두면 트리가 쌓이고, 쌓인 트리는 아무도 틱하지 않는 채로 스크립트 타입을
	// 붙들어 어셈블리 언로드를 막는다.
	if (m_treeInstanceId >= 0)
	{
		ClrHost::Get().DestroyBehaviorTree(m_treeInstanceId);
		m_treeInstanceId = -1;
	}

	const std::vector<ClrHost::ScriptBTNodeDesc> nodes = BTFlatten::Flatten(graph);
	if (nodes.empty())
	{
		Debug->LogError("[BT] 그래프가 비어 있어 트리를 만들지 못했다: " + name);
		return;
	}

	// 저작 블랙보드를 함께 실어 보낸다. 이걸 빠뜨리면 관리 측 트리가 빈 블랙보드로
	// 시작해 노드가 읽는 값이 전부 기본값이 된다 — 기존 BT 에셋이 다르게 움직인다.
	std::vector<ClrHost::ScriptBBEntry> entries;
	if (nullptr != m_pBlackboard) entries = BTFlatten::FlattenBlackBoard(*m_pBlackboard);

	// 노드가 몇 개든, 블랙보드 항목이 몇 개든 경계 통과는 한 번이다.
	m_treeInstanceId = ClrHost::Get().CreateBehaviorTree(
		GetOwner(), nodes.data(), static_cast<int>(nodes.size()),
		entries.empty() ? nullptr : entries.data(), static_cast<int>(entries.size()));

	if (m_treeInstanceId < 0)
	{
		// 조용히 넘기지 않는다 — 트리가 안 서면 그 AI는 아무것도 하지 않는데,
		// 그 모습이 "가만히 있는 캐릭터"라 원인을 짚기 어렵다.
		// 관리 측이 실패 사유를 이미 로그로 남겼다(BehaviorTreeRegistry.Create).
		Debug->LogError("[BT] 트리 생성 실패: " + name);
	}
}

void BehaviorTreeComponent::GraphToBuild()
{
	Benchmark bm;
	std::shared_ptr<BTBuildGraph> cachedGraph = AIManagers->GetBTBuildGraphCache(m_BehaviorTreeGuid);
	if (cachedGraph)
	{
		SendGraphToManaged(*cachedGraph);
	}
	else
	{
		file::path BTpath = DataSystems->GetFilePath(m_BehaviorTreeGuid);
		if (!BTpath.empty() && file::exists(BTpath))
		{
			std::shared_ptr<BTBuildGraph> graph = std::make_shared<BTBuildGraph>();
			std::string parseError;
			const Authoring::ParsedDocument document =
				Authoring::ParsedDocument::ParseFile(BTpath.string(), parseError);
			if (!document)
			{
				Debug->LogError("Behavior Tree parse failed: " + parseError);
				return;
			}
			const Authoring::ReadNode node = document.Root();
			const Authoring::ReadNode nodeList = node["NodeList"];
			if (nodeList && nodeList.IsSequence())
			{
				graph->CleanUp();
				for (const Authoring::ReadNode buildNode : nodeList)
				{
					BTBuildGraphAuthoring::DeserializeSingleNode(*graph, buildNode);
				}
			}
			AIManagers->SetBTBuildGraphCache(m_BehaviorTreeGuid, graph);

			SendGraphToManaged(*graph);
		}
		else
		{
			Debug->LogError("Behavior Tree file does not exist: " + BTpath.string());
		}
	}
	std::cout << "Behavior Tree Build Time: " << bm.GetElapsedTime() << " ms\n";
}


