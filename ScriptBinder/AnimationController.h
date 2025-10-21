#pragma once
#include "Component.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AnimationController.generated.h"
#include "AvatarMask.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "IRegistableEvent.h"
#include <nlohmann/json.hpp>
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
	Animator* m_owner{};
	[[Property]]
	std::vector<std::shared_ptr<AnimationState>> StateVec;
	std::unordered_map<std::string, std::weak_ptr<AnimationState>> m_nameToState;
	std::set<std::string> StateNameSet;

	[[Property]]
	NodeEditor* m_nodeEditor;
	[[Property]]
	std::shared_ptr<AnimationState> m_anyState;
	DirectX::XMMATRIX m_FinalTransforms[512]{};
	DirectX::XMMATRIX m_LocalTransforms[512]{};

	float m_timeElapsed;
	float m_nextTimeElapsed;
	[[Property]]
	AvatarMask* m_avatarMask{};
	float curAnimationProgress = 0.f;
	float preCurAnimationProgress = 0.f;
	float nextAnimationProgress = 0.f;
	float preNextAnimationProgress = 0.f;
private:
	AniTransition* m_curTrans{};
	float blendingTime = 0;
	int m_AnimationIndex = 0;
	int m_nextAnimationIndex = -1;
	//지금일어나는중인 전이 - 블렌드시간 탈출시간등

public:
	bool needBlend = false;
	bool m_isBlend = false;
	//컨트롤러 바꿔치기용
	[[Property]]
	bool useController = true;
	bool m_useLayer = true;

	[[Property]]
	bool useMask = false;
	bool endAnimation = false;

public:
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
	std::shared_ptr<AnimationState> GetAniState();
	AnimationState* CreateState(const std::string& stateName, int animationIndex,bool isAny = false);
	std::shared_ptr<AnimationState> CreateState_UI();

	void DeleteState(std::string stateName);
	void DeleteTransiton(const std::string& fromStateName, const std::string& toStateName);

	AnimationState* FindState(std::string stateName);
	AniTransition* CreateTransition(const std::string& curStateName, const std::string& nextStateName);
	
	AvatarMask* GetAvatarMask() { return m_avatarMask; }
	void CreateMask();
	void ReCreateMask(AvatarMask* mask);//팩토리에서 옮길때 쓸용
	void DeleteAvatarMask(); 


	nlohmann::json Serialize();
	void Deserialize();

	void SetUseLayer(bool _useLayer);
	bool IsUseLayer() { return m_useLayer;}
};

