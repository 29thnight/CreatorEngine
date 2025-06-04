#pragma once
#include "Component.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AnimationController.generated.h"
#include "AvatarMask.h"
#include "imgui-node-editor/imgui_node_editor.h"
class aniState;
class AniTransition;
class AvatarMask;
class Animator;
class NodeEditor;
class AnimationController
{
public:
   ReflectAnimationController
	[[Serializable]]
    AnimationController() = default;
	~AnimationController();
    [[Property]]
    std::string name = "None";
	[[Property]]
	AnimationState* m_curState = nullptr;
	AnimationState* m_nextState = nullptr;
	bool needBlend =false;
	std::unordered_map<std::string, size_t> States;
	[[Property]]
	std::vector<std::shared_ptr<AnimationState>> StateVec;

	std::set<std::string> StateNameSet;

	NodeEditor* m_nodeEditor;
	//ax::NodeEditor::EditorContext* conEdit;
	//어디에서든지 전이가능한 state모음
	[[Property]]
	std::vector<std::shared_ptr<AnimationState>> m_anyStateVec;
	bool BlendingAnimation(float tick);
	Animator* GetOwner() { return m_owner; };
	void SetCurState(std::string stateName);
	void SetNextState(std::string stateName);
	std::shared_ptr<AniTransition> CheckTransition();
	void UpdateState();
	void Update(float tick);
	int GetAnimatonIndexformState(std::string stateName);
	int GetAnimationIndex() { return m_AnimationIndex; }
	int GetNextAnimationIndex() { return m_nextAnimationIndex; }
	AnimationState* CreateState(const std::string& stateName, int animationIndex,bool isAny = false);
	[[Method]]
	void CreateState_UI();

	void DeleteState(std::string stateName);
	void DeleteTransiton(const std::string& fromStateName, const std::string& toStateName);

	AnimationState* FindState(std::string stateName);
	AniTransition* CreateTransition(const std::string& curStateName, const std::string& nextStateName);
	
	AvatarMask* GetAvatarMask() { return &m_avatarMask; }
	void CreateMask();
	void CheckMask();
	Animator* m_owner{};
	float m_timeElapsed;
	float m_nextTimeElapsed;
	float m_isBlend;

	DirectX::XMMATRIX m_FinalTransforms[512]{};

	DirectX::XMMATRIX m_LocalTransforms[512]{};
	[[Property]]
	AvatarMask m_avatarMask{};
private:
	float blendingTime = 0;
	int m_AnimationIndex = 0;
	int m_nextAnimationIndex = -1;
	//지금일어나는중인 전이 - 블렌드시간 탈출시간등
	AniTransition* m_curTrans{};
	
};

