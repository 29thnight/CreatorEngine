#include "Animator.h"
#include "AnimationController.h"
#include "../RenderEngine/Skeleton.h"

void Animator::Update(float tick)
{
	
	if (m_animationControllers.empty()) return;

	for (auto& animationController : m_animationControllers)
	{
		animationController->Update(tick);
	}

	for (auto& param : Parameters)
	{
		if (param.vType == ValueType::Trigger)
		{
			param.ResetTrigger();
		}
	}
}



void Animator::SetAnimation(int index)
{
	m_AnimIndex = index;
	UpdateAnimation();
}

void Animator::UpdateAnimation()
{

	if (m_AnimIndex <= 0)
		m_AnimIndex = 0;

	if (m_AnimIndex >= m_Skeleton->m_animations.size())
		m_AnimIndex = m_Skeleton->m_animations.size() - 1;

	m_AnimIndexChosen = m_AnimIndex;
	m_TimeElapsed = 0;
	
}

void Animator::CreateController(std::string name)
{
	
	
	AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->name = name;
	animationController->CreateMask();
	m_animationControllers.push_back(animationController);
}

void Animator::CreateController_UI()
{
	AnimationController* animationController = new AnimationController();
	animationController->m_owner = this;
	animationController->CreateMask();
	m_animationControllers.push_back(animationController);
}

AnimationController* Animator::GetController(std::string name)
{
	for (auto& Controller : m_animationControllers)
	{
		if (Controller->name == name)
			return Controller;
	}
}



