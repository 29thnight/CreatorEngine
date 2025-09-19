#include "CharacterMovement.h"
#include <algorithm>

CharacterMovement::CharacterMovement() 
	: m_isFall(false)
	, m_speed(0.0f)
	, m_maxSpeed(0.0f)
	, m_acceleration(0.0f)
	, m_staticFriction(0.0f)
	, m_dynamicFriction(0.0f)
	, m_jumpSpeed(0.0f)
	, m_jumpXZAcceleration(0.0f)
	, m_jumpXZDeceleration(0.0f)
	, m_gravityWeight(0.0f)
	, m_minDistance(0.1f)
	, m_velocity(DirectX::SimpleMath::Vector3{})
	, m_outVector(DirectX::SimpleMath::Vector3{})
{
}

CharacterMovement::~CharacterMovement()
{
}

void CharacterMovement::Initialize(const CharacterMovementInfo& info)
{
	m_maxSpeed = info.maxSpeed;
	m_acceleration = info.acceleration;
	m_staticFriction = info.staticFriction;
	m_dynamicFriction = info.dynamicFriction;
	m_jumpSpeed = info.jumpSpeed;
	m_jumpXZAcceleration = info.jumpXZAcceleration;
	m_jumpXZDeceleration = info.jumpXZDeceleration;
	m_gravityWeight = info.gravityWeight;
	m_minDistance = 0.1f;
}

void CharacterMovement::Update(float deltaTime, const DirectX::SimpleMath::Vector3& input, bool isDynamic)
{
	if (!m_isFall)
	{
		if (input.x == 0&&!isDynamic) {
			m_velocity.x = std::lerp(m_velocity.x, 0.0f, m_staticFriction);
		}
		else {
			m_velocity.x = std::lerp(m_velocity.x, 0.0f, m_dynamicFriction);
		}

		if (input.z == 0 && !isDynamic) {
			m_velocity.z = std::lerp(m_velocity.z, 0.0f, m_staticFriction);
		}
		else {
			m_velocity.z = std::lerp(m_velocity.z, 0.0f, m_dynamicFriction);
		}

		m_velocity.x += input.x * m_acceleration * deltaTime;
		m_velocity.z += input.z * m_acceleration * deltaTime;
	}
	else {
		if (input.x == 0) {
			m_velocity.x = std::lerp(m_velocity.x, 0.0f, m_jumpXZDeceleration);
		}
		if (input.z == 0)
		{
			m_velocity.z = std::lerp(m_velocity.z, 0.0f, m_jumpXZDeceleration);
		}
		m_velocity.x += input.x * m_jumpXZAcceleration * deltaTime;
		m_velocity.z += input.z * m_jumpXZAcceleration * deltaTime;
	}

	if (input.y != 0 && !m_isFall)
	{
		m_jumpSpeed = input.y; //캐릭터 컴포넌트에서 넘겨줄값
		Jump();
	}

	CumputeMovement(deltaTime);
}

void CharacterMovement::Jump()
{
	m_velocity.y = m_jumpSpeed;
}

void CharacterMovement::CumputeMovement(float deltaTime)
{
	if (m_isFall)
	{
		m_velocity.y -= m_gravityWeight * deltaTime;
	}

	m_speed = sqrtf(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);

	if (m_speed > m_maxSpeed)
	{
		m_speed = m_maxSpeed;
	}

	float triangleFunc = sqrtf(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
	if (triangleFunc >= 0.001f)
	{
		m_outVector.x = (m_velocity.x / triangleFunc)*m_speed;
		m_outVector.z = (m_velocity.z / triangleFunc)*m_speed;
	}
	else
	{
		m_outVector.x = 0.0f;
		m_outVector.z = 0.0f;
	}

	m_outVector.y = m_velocity.y;


	if (onKnockback)
	{
		if (m_isFall)
		{
			knockbackVelocity.y -= m_gravityWeight * deltaTime;
		}
		m_outVector = knockbackVelocity;
	}

}

void CharacterMovement::LimitVelocity()
{
	m_velocity.x = std::clamp(m_velocity.x, -m_maxSpeed, m_maxSpeed);
	m_velocity.z = std::clamp(m_velocity.z, -m_maxSpeed, m_maxSpeed);
}

void CharacterMovement::OutputPxVector3(physx::PxVec3& dir)
{
	dir.x = m_outVector.x;
	dir.y = m_outVector.y;
	dir.z = m_outVector.z;
}
