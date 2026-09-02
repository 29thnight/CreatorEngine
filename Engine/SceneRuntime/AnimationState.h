#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "AniBehavior.h"

class AnimationController;
class AnimationState
{	
   public:
   static consteval auto reflect()
   {
       using Self = AnimationState;
       return meta::schema<Self>(
           meta::field<&Self::m_name>,
           meta::field<&Self::behaviourName>,
           meta::field<&Self::Transitions>,
           meta::field<&Self::index>,
           meta::field<&Self::AnimationIndex>,
           meta::field<&Self::animationSpeed>,
           meta::field<&Self::multiplerAnimationSpeed>,
           meta::field<&Self::animationSpeedParameterName>,
           meta::field<&Self::m_isAny>,
           meta::field<&Self::useMultipler>);
   }
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
	std::shared_ptr<AniBehavior> behaviour{};
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
	float m_animationTimeElapsed = 0;
	bool m_isAny = false;
	bool useMultipler = false;
};

