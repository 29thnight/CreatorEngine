#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "AnimationState.generated.h"
class AnimationController;
class AniBehaviour;
class AnimationState
{	
public:
   ReflectAnimationState
	[[Serializable]]
	AnimationState() = default;
	~AnimationState();
	AnimationState(AnimationController* Owner, std::string name) : m_ownerController(Owner), m_name(name) {}

	std::vector<AniTransition*> FindTransitions(const std::string& toStateName);

	void SetBehaviour(std::string name);
	[[Property]]
	std::string m_name{};

	std::string behaviourName{};
	AniBehaviour* behaviour{};
	AnimationController* m_ownerController{};
	[[Property]]
	std::vector<std::shared_ptr<AniTransition>> Transitions;
	[[Property]]
	int index =0; 
	[[Property]]
	int AnimationIndex = -1;
	[[Property]]
	bool m_isAny = false;
	//������ �ִϸ��̼� �ð� ����ü �и��� ��ĥ������
	float m_animationTimeElapsed = 0;
};

