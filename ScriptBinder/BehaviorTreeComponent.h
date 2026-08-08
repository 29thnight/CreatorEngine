#pragma once
#include "imgui-node-editor\imgui_node_editor.h"
#include "Component.h"
#include "IAIComponent.h"
#include "AIManager.h"
#include "ClrHost.h"
#include "BehaviorTreeComponent.generated.h"

using namespace BT;

class BehaviorTreeComponent : 
	public Component, public IAIComponent
{
public:
   ReflectBehaviorTreeComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(BehaviorTreeComponent)

	[[Property]]
	std::string name; // BT ¿¡¼Â ÀÌ¸§
	[[Property]]
	std::string blackBoardName;

	// IAIComponent ÀÎÅÍÆäÀÌ½º ±¸Çö
	void Initialize() override;
	void Awake() override;
	void InternalAIUpdate(float deltaSecond) override;
	void OnDestroy() override;
	BlackBoard* GetBlackBoard();
private:
	// Behavior Tree °ü·Ã ¸Ş¼­µå
private:
	// ì €ì‘ ê·¸ë˜í”„ë¥¼ í‰í‰í•˜ê²Œ í´ì„œ ê´€ë¦¬ ì¸¡ì— ë„˜ê¸°ê³  ì¸ìŠ¤í„´ìŠ¤ idë¥¼ ë°›ëŠ”ë‹¤ (PHASE 9-8).
	// ì¡°ë¦½ê³¼ í‹±ì€ ì „ë¶€ ê´€ë¦¬ ì¸¡ì—ì„œ ëë‚œë‹¤ â€” ë„¤ì´í‹°ë¸ŒëŠ” idë§Œ ë“ ë‹¤.
	void SendGraphToManaged(const BTBuildGraph& graph);

public:
	void GraphToBuild();
	void ClearTree()
	{
		// ê´€ë¦¬ ì¸¡ íŠ¸ë¦¬ë¥¼ ë‚´ë¦°ë‹¤. ë„¤ì´í‹°ë¸Œì—ëŠ” ë” ì´ìƒ íŠ¸ë¦¬ ì‹¤ì²´ê°€ ì—†ë‹¤.
		if (m_treeInstanceId < 0) return;
		ClrHost::Get().DestroyBehaviorTree(m_treeInstanceId);
		m_treeInstanceId = -1;
	}


public:
	// Behavior TreeÀÇ GUID Á÷·ÄÈ­ ¹× ¿ªÁ÷·ÄÈ­¸¦ À§ÇÑ ±¸Á¶
	[[Property]]
	FileGuid m_BehaviorTreeGuid; // Behavior TreeÀÇ GUID
	[[Property]]
	FileGuid m_BlackBoardGuid; // ºí·¢º¸µåÀÇ GUID
private:
	BlackBoard* m_pBlackboard; // ºí·¢º¸µå µ¥ÀÌÅÍ
	// ê´€ë¦¬ ì¸¡ íŠ¸ë¦¬ ì¸ìŠ¤í„´ìŠ¤ id. ìŒìˆ˜ë©´ íŠ¸ë¦¬ê°€ ì—†ë‹¤.
	// m_rootÂ·m_builtê°€ ìˆë˜ ìë¦¬ë‹¤ â€” íŠ¸ë¦¬ ì‹¤ì²´ëŠ” ì´ì œ ê´€ë¦¬ ì¸¡ì— ìˆë‹¤.
	int m_treeInstanceId{ -1 };
};
