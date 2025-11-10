#include "TestEffect.h"
#include "pch.h"
#include "InputManager.h"
#include "EffectComponent.h"
#include "SceneManager.h"
#include "CharacterControllerComponent.h"
#include "Animator.h"
void TestEffect::Start()
{
	controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	m_animator = GetOwner()->GetComponent<Animator>();
}

void TestEffect::Update(float tick)
{
	if (controller)
	{
		controller->SetBaseSpeed(moveSpeed);
	}
}

void TestEffect::Move(Mathf::Vector2 dir)
{

	if (!m_isCallStart) return;
	if (!controller) return;


	controller->Move(dir);
	if (controller->IsOnMove() && dir.LengthSquared() > 1e-6f)
	{
		if (m_animator)
			m_animator->SetParameter("OnMove", true);
	}
	else
	{
		if (m_animator)
			m_animator->SetParameter("OnMove", false);
	}
}

