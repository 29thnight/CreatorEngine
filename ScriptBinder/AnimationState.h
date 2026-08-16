#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "AniBehavior.h"
#include <nlohmann/json.hpp>

class AnimationController;
class AnimationState
{	
public:
   static consteval auto describe()
   {
       return meta::describe<AnimationState>(
           meta::member<&AnimationState::m_name>(),
           meta::member<&AnimationState::behaviourName>(),
           meta::member<&AnimationState::Transitions>(),
           meta::member<&AnimationState::index>(),
           meta::member<&AnimationState::AnimationIndex>(),
           meta::member<&AnimationState::animationSpeed>(),
           meta::member<&AnimationState::multiplerAnimationSpeed>(),
           meta::member<&AnimationState::animationSpeedParameterName>(),
           meta::member<&AnimationState::m_isAny>(),
           meta::member<&AnimationState::useMultipler>());
   }
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
	nlohmann::json Serialize();
	AnimationState Deserialize();

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

