#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "AniBehavior.h"

class AnimationController;
class ConditionParameter;
class [[reflgen::reflect]] AnimationState
{	
   public:
public:
	AnimationState();
   ~AnimationState();
   AnimationState(AnimationController* Owner, std::string name);

	std::vector<AniTransition*> FindTransitions(const std::string& toStateName);

	void ClearBehaviour()
	{
		ResetBehaviour();
		behaviourName.clear();
	}

	void ResetBehaviour()
	{
		behaviour.reset();
		behaviour = nullptr;
	}
	void SetBehaviour(std::string name, bool isReload = false);
	void UpdateAnimationSpeed();
public:
	std::string m_name{};
	std::string behaviourName{};

	[[reflgen::ignore]]
	std::shared_ptr<AniBehavior> behaviour{};

	[[reflgen::ignore]]
	AnimationController* m_ownerController{};

	std::vector<std::shared_ptr<AniTransition>> Transitions;
	int index =0; 
	int AnimationIndex = 0;
	
	//기본속도
	float animationSpeed = 1;
	//파라미터로 더 곱해줄 속도 이동속도 비례,공격속도비례
	float multiplerAnimationSpeed = 1;
	std::string animationSpeedParameterName = "None";

	//상태의 애니메이션 시간 상하체 분리후 합칠떄쓸용
	[[reflgen::ignore]]
	float m_animationTimeElapsed = 0;

	bool m_isAny = false;
	bool useMultipler = false;

	// Runtime-only handle. Parameter layout changes invalidate the cached index;
	// direct name edits are checked against the resolved entry on every read.
	[[reflgen::ignore]]
	std::size_t m_speedParameterIndex{ static_cast<std::size_t>(-1) };

	[[reflgen::ignore]]
	std::uint64_t m_speedParameterVersion{};

	[[reflgen::ignore]]
	std::string m_speedParameterName{};
};

