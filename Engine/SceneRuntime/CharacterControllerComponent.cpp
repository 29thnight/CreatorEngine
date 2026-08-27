#include "CharacterControllerComponent.h"
#include "CharacterControllerSystem.h"

void CharacterControllerComponent::OnStart()
{
	m_transform = &GetOwner()->Transform_();
	
	//m_fBaseSpeed = m_movementInfo.maxSpeed;
	m_fBaseAcceleration = m_movementInfo.acceleration;
	m_bMoveRestrict.fill(false);
}

// íŠ¸ëž™ C3 â€” CharacterControllerSystem ë“±ë¡/í•´ì§€. Awake/OnDestroy(ìœ„, ì»´í¬ë„ŒíŠ¸ë‹¹
// 1íšŒ ê²Œì´íŠ¸)ê°€ ì•„ë‹ˆë¼ ì”¬ íŽ¸ìž…/ì´íƒˆ í›…ì„ ì“°ëŠ” ì´ìœ ëŠ” CharacterControllerSystem.h
// ìƒë‹¨ ì£¼ì„ ì°¸ì¡° â€” DDOL ì˜¤ë¸Œì íŠ¸ê°€ ì”¬ì„ ê±´ë„ ë•Œë„ ë§¤ë²ˆ ë‹¤ì‹œ ë¶ˆë ¤ì•¼ í•˜ê¸° ë•Œë¬¸ì´ë‹¤.
// ì‹¤ì œ íŒŒê´´ ê²½ë¡œ(Scene::FlushPendingDestroyÂ·PrefabUtility::ApplyComponentDiff)ë„
// ì‹¤ íŒŒê´´(OnDestroy) ì§ì „ì— OnRemovingFromSceneì„ ë¨¼ì € ë¶€ë¥´ë¯€ë¡œ, ì´ ì‹œìŠ¤í…œì—ì„œ
// ë¹ ì§€ëŠ” ì‹œì ì´ í•­ìƒ ì‹¤ íŒŒê´´ë³´ë‹¤ ë¨¼ì €ë‹¤.
void CharacterControllerComponent::OnAddedToScene()
{
	CharacterControllerSystems->Register(this);
}

void CharacterControllerComponent::OnRemovingFromScene()
{
	CharacterControllerSystems->Unregister(this);
}

void CharacterControllerComponent::OnFixedUpdate(float fixedDeltaTime)
{
	//µî·ÏµÇÁö ¾ÊÀº ÄÁÆ®·Ñ·¯
	if (m_controllerInfo.id == 0)
	{
		return;
	}



	math::vector3 input{};
	input.x = m_moveInput.x;
	input.z = m_moveInput.y;
	/*if(m_isKnockBack)
	{ 
		input.y = JumpPower;
	}*/

	//ÄÉ¸¯ÅÍ ÄÁÆ®·Ñ·¯
	//todo : ÀÌµ¿ ºÒ°¡ÇÑ ½ºÅÏ »óÅÂ Ã¼Å© ÇÊ¿ä --> ÇÊ¿ä½Ã Ãß°¡

	m_bOnMove = input != math::vector3{};

	input = math::normalize(input);

	CharactorControllerInputInfo inputInfo;
	inputInfo.id = m_controllerInfo.id;
	inputInfo.input = input;
	auto component = GetOwner()->GetComponent<RigidBodyComponent>();
	if (!component) return;

	inputInfo.isDynamic = component->GetBodyType() == EBodyType::DYNAMIC;
	Physics->AddInputMove(inputInfo);


	Physics->SetCharacterMovementMaxSpeed(inputInfo, m_movementInfo.maxSpeed);

	if (m_useAutomaticRotation)
	{
		constexpr float rotationOffsetSquare = 0.5f * 0.5f;
		math::vector3 lookDir = m_hasCustomLookDirection ? m_lookDirection : input;
		lookDir.y = 0.f;

		if (math::length_sq(lookDir) >= rotationOffsetSquare) {
			lookDir = math::normalize(lookDir);

			// yaw °è»ê: Z°¡ ¾ÕÀÌ¹Ç·Î (z, x) ¼ø¼­ ÁÖÀÇ
			float targetYaw = std::atan2(lookDir.z, lookDir.x) - math::half_pi;  // ¶óµð¾È °ª
			targetYaw = -targetYaw;

			// ÇöÀç È¸Àü¿¡¼­ yaw¸¸ ÃßÃâ
			const math::vector3 currentEuler =
				math::to_euler(m_transform->GetWorldQuaternion());
			float currentYaw = currentEuler.y;

			// Slerp ´ë½Å float º¸°£µµ °¡´ÉÇÏÁö¸¸, Quaternion À¯ÁöÇÏ·Á¸é ÀÌ·¸°Ô:
			const math::quaternion currentRot = math::quaternion_from_pitch_yaw_roll(0.0f, currentYaw, 0.0f);
			const math::quaternion targetRot = math::quaternion_from_pitch_yaw_roll(0.0f, targetYaw, 0.0f);

			const math::quaternion resultRot = math::slerp(currentRot, targetRot, m_rotationSpeed * fixedDeltaTime);
			m_transform->SetRotation(resultRot);
		}
	}

	ClearLookDirection();
}

void CharacterControllerComponent::OnLateUpdate(float fixedDeltaTime)
{
	m_movementInfo.maxSpeed = m_fBaseSpeed * m_fFinalMultiplierSpeed;
	m_movementInfo.acceleration = m_fBaseAcceleration * m_fFinalMultiplierSpeed;
	//m_fFinalMultiplierSpeed = 1.0f; 
}


void CharacterControllerComponent::ForcedSetPosition(const math::vector3& pos)
{
	PhysicsManagers->SetControllerPosition(m_controllerInfo.id, pos);
}

void CharacterControllerComponent::SetAutomaticRotation(bool useAuto)
{
	m_useAutomaticRotation = useAuto;
}

void CharacterControllerComponent::TriggerForcedMove(
	const math::vector3& initialVelocity, float duration)
{	
	Physics->ApplyForcedMoveToCCT(
		m_controllerInfo.id, initialVelocity, duration);
}
void CharacterControllerComponent::StopForcedMove()
{
	Physics->StopForcedMoveOnCCT(m_controllerInfo.id);
}

bool CharacterControllerComponent::IsInForcedMove() const
{
	return Physics->IsInForcedMove(m_controllerInfo.id);
}

void CharacterControllerComponent::SetLookDirection(const math::vector3& direction)
{
	m_lookDirection = direction;
	m_hasCustomLookDirection = true;
}

void CharacterControllerComponent::ClearLookDirection()
{
	m_hasCustomLookDirection = false;
}

void CharacterControllerComponent::OnTriggerEnter(ICollider* other)
{
	++m_collsionCount;
}

void CharacterControllerComponent::OnTriggerStay(ICollider* other)
{
}

void CharacterControllerComponent::OnTriggerExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		--m_collsionCount;
	}
}

void CharacterControllerComponent::OnCollisionEnter(ICollider* other)
{
	++m_collsionCount;
}

void CharacterControllerComponent::OnCollisionStay(ICollider* other)
{
}

void CharacterControllerComponent::OnCollisionExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		--m_collsionCount;
	}
}
