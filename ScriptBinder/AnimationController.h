#pragma once
#include "Component.h"
#include "AniTransition.h"
#include "ConditionParameter.h"
#include "AnimationState.h"
#include "AnimationController.generated.h"
#include "AvatarMask.h"
class aniState;
class AniTransition;
class AvatarMask;
class Animator;
class AnimationController
{
public:
   ReflectAnimationController
	[[Serializable]]
    AnimationController() = default;

    [[Property]]
    std::string name;
	[[Property]]
	AnimationState* m_curState = nullptr;
	AnimationState* m_nextState = nullptr;
	bool needBlend =false;
	std::unordered_map<std::string, size_t> States;
	[[Property]]
	std::vector<std::shared_ptr<AnimationState>> StateVec;

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
	AnimationState* CreateState(const std::string& stateName, int animationIndex);
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
private:
	float blendingTime = 0;
	int m_AnimationIndex = 0;
	int m_nextAnimationIndex = -1;
	//�����Ͼ������ ���� - ����ð� Ż��ð���
	AniTransition* m_curTrans{};
	[[Property]]
	AvatarMask m_avatarMask{};
};

