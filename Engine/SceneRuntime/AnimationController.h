#include "../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/EngineOrderedSet.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "Component.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AvatarMask.h"
#include "AnimatorSystem.h"
#include "../RenderEngine/LocalPose.h"
#include <mathematics/matrix4x4.hpp>
#include "../Utility_Framework/Foundation/StandardLibrary.h"
class AniTransition;
class AvatarMask;
class Animator;
class [[reflgen::reflect, reflgen::public_methods]] AnimationController : public own::enable_owner_from_this<AnimationController>
{
   public:
public:
    AnimationController();
	~AnimationController();
    AnimationController(const AnimationController&) = delete;
    AnimationController& operator=(const AnimationController&) = delete;
    AnimationController(AnimationController&&) = delete;
    AnimationController& operator=(AnimationController&&) = delete;
	std::string name = "None";
	// Legacy scene key. Runtime selection belongs to ControllerPlayback.
	AnimationState* m_curState = nullptr;

	[[reflgen::ignore]]
	Animator* m_owner{};

	ce::dynamic_array<own::shared_owner<AnimationState>> StateVec;

	[[reflgen::ignore]]
	ce::engine_node_map<std::string, own::weak_owner<AnimationState>> m_nameToState;

	[[reflgen::ignore]]
	ce::engine_ordered_set<std::string> StateNameSet;

	own::shared_owner<AnimationState> m_anyState;
	AvatarMask* m_avatarMask{};
private:
	[[reflgen::ignore]]
	std::uint64_t m_playbackId{};

public:
	//컨트롤러 바꿔치기용
	bool useController = true;

	[[reflgen::ignore]]
	bool m_useLayer = true;

	bool useMask = false;
	// Opt-in overlay: evaluate a delta from this layer's clip at time zero.
	bool m_additive = false;

public:
	[[nodiscard]] ControllerPlayback& GetPlayback();
	[[nodiscard]] const ControllerPlayback& GetPlayback() const;
	[[nodiscard]] AnimationState* GetCurrentState() const { return GetPlayback().currentState; }
	[[nodiscard]] bool IsBlending() const { return GetPlayback().isBlending; }
	[[nodiscard]] bool HasEndedAnimation() const { return GetPlayback().endAnimation; }
	void MarkAnimationEnded() { GetPlayback().endAnimation = true; }
	[[reflgen::script_internal]]
	void OnBeforeSerialize();
	bool BlendingAnimation(float tick);
	Animator* GetOwner() { return m_owner; };
	void SetCurState(std::string stateName);
	void SetNextState(std::string stateName);
	own::shared_owner<AniTransition> CheckTransition();
	void UpdateState();
	void Update(float tick);
	int GetAnimatonIndexformState(std::string stateName);
	int GetAnimationIndex() const { return GetPlayback().animationIndex; }
	int GetNextAnimationIndex() const { return GetPlayback().nextAnimationIndex; }
	own::shared_owner<AnimationState> GetAniState();
	AnimationState* CreateState(const std::string& stateName, int animationIndex,bool isAny = false);
	own::shared_owner<AnimationState> CreateState_UI();

	void DeleteState(std::string stateName);
	void DeleteTransiton(const std::string& fromStateName, const std::string& toStateName);

	AnimationState* FindState(std::string stateName);
	AniTransition* CreateTransition(const std::string& curStateName, const std::string& nextStateName);

	AvatarMask* GetAvatarMask() { return m_avatarMask; }
	void CreateMask();
	void ReCreateMask(AvatarMask* mask);//팩토리에서 옮길때 쓸용
	void DeleteAvatarMask();


	void SetUseLayer(bool _useLayer);
	bool IsUseLayer() { return m_useLayer;}
};
