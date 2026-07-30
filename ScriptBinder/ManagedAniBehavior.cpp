#ifndef DYNAMICCPP_EXPORTS
#include "ManagedAniBehavior.h"
#include "AnimationController.h"
#include "Animator.h"
#include "GameObject.h"

GameObject* ManagedAniBehavior::ResolveOwner() const
{
	if (nullptr == m_ownerController) return nullptr;

	Animator* animator = m_ownerController->GetOwner();
	return (nullptr != animator) ? animator->GetOwner() : nullptr;
}
#endif // !DYNAMICCPP_EXPORTS
