#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "AniTransition.h"
#include "AniBehavior.h"
#include "AnimationState.generated.h"
class AnimationController;

class AnimationState
{	
public:
   ReflectAnimationState
	[[Serializable]]
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
	[[Property]]
	std::string m_name{};
	[[Property]]
	std::string behaviourName{};
	std::shared_ptr<AniBehavior> behaviour{};
	AnimationController* m_ownerController{};
	[[Property]]
	std::vector<std::shared_ptr<AniTransition>> Transitions;
	[[Property]]
	int index =0; 
	[[Property]]
	int AnimationIndex = 0;
	[[Property]]
	bool m_isAny = false;
	//������ �ִϸ��̼� �ð� ����ü �и��� ��ĥ������
	float m_animationTimeElapsed = 0;
};

