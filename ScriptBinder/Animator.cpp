#include "Animator.h"
#include "AnimationController.h"
#include "../RenderEngine/Skeleton.h"

void Animator::Update(float tick)
{
	if (m_animationController == nullptr) return;
	m_animationController->Update(tick);
}

std::string Animator::GetcurAnimation()
{
	return m_Skeleton->m_animations[m_AnimIndexChosen].m_name;
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

void Animator::CreateController()
{
	//m_animationController = std::make_shared<AnimationController>();
	m_animationController = new AnimationController();
	m_animationController->m_owner = this;
	m_animationController->CreateMask();
	
}
