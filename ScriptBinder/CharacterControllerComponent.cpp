#include "CharacterControllerComponent.h"

void CharacterControllerComponent::OnStart()
{
	m_transform = &(GetOwner()->m_transform);
	
	//m_fBaseSpeed = m_movementInfo.maxSpeed;
	m_fBaseAcceleration = m_movementInfo.acceleration;
	m_bMoveRestrict.fill(false);
}

void CharacterControllerComponent::OnFixedUpdate(float fixedDeltaTime)
{
	//��ϵ��� ���� ��Ʈ�ѷ�
	if (m_controllerInfo.id == 0)
	{
		return;
	}



	DirectX::SimpleMath::Vector3 input = DirectX::SimpleMath::Vector3{ 0.f, 0.f, 0.f };
	input.x = m_moveInput.x;
	input.z = m_moveInput.y;
	/*if(m_isKnockBack)
	{ 
		input.y = JumpPower;
	}*/

	//�ɸ��� ��Ʈ�ѷ�
	//todo : �̵� �Ұ��� ���� ���� üũ �ʿ� --> �ʿ�� �߰�

	m_bOnMove = input != DirectX::SimpleMath::Vector3{ 0.f, 0.f, 0.f };

	input.Normalize();

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
		float inputSquare = input.LengthSquared();
		DirectX::SimpleMath::Vector3 flatInput = input;
		flatInput.y = 0.f;

		if (flatInput.LengthSquared() >= rotationOffsetSquare) {
			flatInput.Normalize();

			// yaw ���: Z�� ���̹Ƿ� (z, x) ���� ����
			float targetYaw = std::atan2(flatInput.z,flatInput.x) - (XM_PI / 2.0);  // ���� ��
			targetYaw = -targetYaw;

			// ���� ȸ������ yaw�� ����
			DirectX::SimpleMath::Quaternion quator = m_transform->GetWorldQuaternion();
			DirectX::SimpleMath::Vector3 currentEuler = quator.ToEuler();
			float currentYaw = currentEuler.y;

			// Slerp ��� float ������ ����������, Quaternion �����Ϸ��� �̷���:
			DirectX::SimpleMath::Quaternion currentRot = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(currentYaw, 0.0f, 0.0f);
			DirectX::SimpleMath::Quaternion targetRot = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(targetYaw, 0.0f, 0.0f);

			DirectX::SimpleMath::Quaternion resultRot = DirectX::SimpleMath::Quaternion::Slerp(currentRot, targetRot, m_rotationSpeed * fixedDeltaTime);
			m_transform->SetRotation(resultRot);
		}
	}

}

void CharacterControllerComponent::OnLateUpdate(float fixedDeltaTime)
{
	m_movementInfo.maxSpeed = m_fBaseSpeed * m_fFinalMultiplierSpeed;
	m_movementInfo.acceleration = m_fBaseAcceleration * m_fFinalMultiplierSpeed;
	//m_fFinalMultiplierSpeed = 1.0f; 
}


void CharacterControllerComponent::SetAutomaticRotation(bool useAuto)
{
	m_useAutomaticRotation = useAuto;
}

//void CharacterControllerComponent::TriggerForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration, int curveType)
//{	
//	Physics->ApplyForcedMoveToCCT(m_controllerInfo.id, initialVelocity, duration, curveType);
//}
void CharacterControllerComponent::TriggerForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration)
{
	Physics->ApplyForcedMoveToCCT(m_controllerInfo.id, initialVelocity, duration);
}

void CharacterControllerComponent::StopForcedMove()
{
	Physics->StopForcedMoveOnCCT(m_controllerInfo.id);
}

bool CharacterControllerComponent::IsInForcedMove() const
{
	return Physics->IsInForcedMove(m_controllerInfo.id);
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
