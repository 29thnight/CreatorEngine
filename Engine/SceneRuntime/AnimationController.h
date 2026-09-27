#pragma once
#include "Component.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AvatarMask.h"
#include "AnimatorSystem.h"
#include "../RenderEngine/LocalPose.h"
#include <mathematics/matrix4x4.hpp>
#include <cstdint>
#include <memory>
class AniTransition;
class AvatarMask;
class Animator;
class AnimationController : public std::enable_shared_from_this<AnimationController>
{
   public:
   static consteval auto reflect()
   {
       using Self = AnimationController;
       return meta::schema<Self>(
           meta::field<&Self::name>,
           meta::field<&Self::m_curState>,
           meta::field<&Self::StateVec>,
           meta::field<&Self::m_anyState>,
           meta::field<&Self::m_avatarMask>,
           meta::field<&Self::useController>,
           meta::field<&Self::useMask>,
           meta::field<&Self::m_additive>);
   }
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
	Animator* m_owner{};
	std::vector<std::shared_ptr<AnimationState>> StateVec;
	std::unordered_map<std::string, std::weak_ptr<AnimationState>> m_nameToState;
	std::set<std::string> StateNameSet;

	std::shared_ptr<AnimationState> m_anyState;
	AvatarMask* m_avatarMask{};
private:
	std::uint64_t m_playbackId{};

public:
	//컨트롤러 바꿔치기용
	bool useController = true;
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
	void OnBeforeSerialize();
	bool BlendingAnimation(float tick);
	Animator* GetOwner() { return m_owner; };
	void SetCurState(std::string stateName);
	void SetNextState(std::string stateName);
	std::shared_ptr<AniTransition> CheckTransition();
	void UpdateState();
	void Update(float tick);
	int GetAnimatonIndexformState(std::string stateName);
	int GetAnimationIndex() const { return GetPlayback().animationIndex; }
	int GetNextAnimationIndex() const { return GetPlayback().nextAnimationIndex; }
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


	void SetUseLayer(bool _useLayer);
	bool IsUseLayer() { return m_useLayer;}
};

