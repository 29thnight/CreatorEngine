#pragma once
#include <physx/PxPhysicsAPI.h>
#include "PhysicsCommon.h"


class CharacterMovement
{
public:
	CharacterMovement();
	~CharacterMovement();
	
	void Initialize(const CharacterMovementInfo& info);
	void Update(float deltaTime,const DirectX::SimpleMath::Vector3& input,bool isDynamic);
	void Jump();

	void CumputeMovement(float deltaTime);
	void LimitVelocity();
	void OutputPxVector3(physx::PxVec3& dir);

	inline const DirectX::SimpleMath::Vector3& GetOutVector() const { return m_outVector; }
	
	inline const DirectX::SimpleMath::Vector3& GetVelocity() const { return m_velocity; }
	inline const bool& GetIsFall() const { return m_isFall; }
	inline const float& GetSpeed() const { return m_speed; }
	inline const float& GetMaxSpeed() const { return m_maxSpeed; }
	inline const float& GetAcceleration() const { return m_acceleration; }
	inline const float& GetStaticFriction() const { return m_staticFriction; }
	inline const float& GetDynamicFriction() const { return m_dynamicFriction; }
	inline const float& GetJumpSpeed() const { return m_jumpSpeed; }
	inline const float& GetJumpXZAcceleration() const { return m_jumpXZAcceleration; }
	inline const float& GetJumpXZDeceleration() const { return m_jumpXZDeceleration; }
	inline const float& GetGravityWeight() const { return m_gravityWeight; }
	inline void SetVelocity(const DirectX::SimpleMath::Vector3& velocity) { m_velocity = velocity; }
	inline void SetIsFall(const bool& isFall) { m_isFall = isFall; }
	inline void SetMaxSpeed(const float& maxSpeed) { m_maxSpeed = maxSpeed; }
	inline void SetAcceleration(const float& acceleration) { m_acceleration = acceleration; }
	inline void SetStaticFriction(const float& staticFriction) { m_staticFriction = staticFriction; }
	inline void SetDynamicFriction(const float& dynamicFriction) { m_dynamicFriction = dynamicFriction; }
	inline void SetJumpSpeed(const float& jumpSpeed) { m_jumpSpeed = jumpSpeed; }
	inline void SetJumpXZAcceleration(const float& jumpXZAcceleration) { m_jumpXZAcceleration = jumpXZAcceleration; }
	inline void SetJumpXZDeceleration(const float& jumpXZDeceleration) { m_jumpXZDeceleration = jumpXZDeceleration; }
	inline void SetGravityWeight(const float& gravityWeight) { m_gravityWeight = gravityWeight; }


private:
	bool m_isFall;

	DirectX::SimpleMath::Vector3 m_velocity;
	DirectX::SimpleMath::Vector3 m_outVector;

	float m_speed;
	float m_maxSpeed;
	float m_acceleration;
	float m_staticFriction;
	float m_dynamicFriction;
	float m_jumpSpeed;
	float m_jumpXZAcceleration;
	float m_jumpXZDeceleration;
	float m_gravityWeight;
	float m_minDistance;

};

