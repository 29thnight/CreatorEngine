#pragma once
#include "imgui-node-editor\imgui_node_editor.h"
#include "Component.h"
#include "IAIComponent.h"
#include "AIManager.h"
#include "ClrHost.h"

using namespace BT;

class BehaviorTreeComponent : 
	public meta::identity<BehaviorTreeComponent, Component>, public IAIComponent
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::name>,
           meta::field<&Self::blackBoardName>,
           meta::field<&Self::m_BehaviorTreeGuid>,
           meta::field<&Self::m_BlackBoardGuid>);
   }
public:
	BehaviorTreeComponent() = default;

	std::string name; // BT 에셋 이름
	std::string blackBoardName;

	// IAIComponent 인터페이스 구현
	void Initialize() override;
	void OnInitialized() override;
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;
	void InternalAIUpdate(float deltaSecond) override;
	void OnUninitializing() override;
	BlackBoard* GetBlackBoard();
private:
	// Behavior Tree 관련 메서드
private:
	// ???洹몃옒?꾨? ?됲룊?섍쾶 ?댁꽌 愿由?痢≪뿉 ?섍린怨??몄뒪?댁뒪 id瑜?諛쏅뒗??(PHASE 9-8).
	// 議곕┰怨??깆? ?꾨? 愿由?痢≪뿉???앸궃?????ㅼ씠?곕툕??id留??좊떎.
	void SendGraphToManaged(const BTBuildGraph& graph);

public:
	void GraphToBuild();
	void ClearTree()
	{
		// 愿由?痢??몃━瑜??대┛?? ?ㅼ씠?곕툕?먮뒗 ???댁긽 ?몃━ ?ㅼ껜媛 ?녿떎.
		if (m_treeInstanceId < 0) return;
		ClrHost::Get().DestroyBehaviorTree(m_treeInstanceId);
		m_treeInstanceId = -1;
	}


public:
	// Behavior Tree의 GUID 직렬화 및 역직렬화를 위한 구조
	FileGuid m_BehaviorTreeGuid; // Behavior Tree의 GUID
	FileGuid m_BlackBoardGuid; // 블랙보드의 GUID
private:
	BlackBoard* m_pBlackboard; // 블랙보드 데이터
	// 愿由?痢??몃━ ?몄뒪?댁뒪 id. ?뚯닔硫??몃━媛 ?녿떎.
	// m_root쨌m_built媛 ?덈뜕 ?먮━?????몃━ ?ㅼ껜???댁젣 愿由?痢≪뿉 ?덈떎.
	int m_treeInstanceId{ -1 };
};
