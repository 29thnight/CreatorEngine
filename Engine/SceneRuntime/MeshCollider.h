#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"

class MeshColliderComponent : public meta::identity<MeshColliderComponent, Component>, public ICollider
{
   public:
   // C1: m_Info(ConvexMeshColliderInfo)를 필드 목록에서 뺐다.
   //
   // 그 타입은 원시 포인터(Vector3* vertices)와 그 길이를 담은 런타임 구조체라
   // 디스크에 적을 수 있는 값이 아니다. 실제로 직렬화기는 이 필드를
   // "[not support type]" 문자열로 적고 로드 때는 버려 왔다 — 즉 **한 번도
   // 왕복한 적이 없다**(씬·프리팹 저장 인스턴스 0건이라 디스크에도 흔적이 없고,
   // 미지원 타입을 컴파일 오류로 승격하고서야 드러났다).
   //
   // 이 저장소의 관례는 BoxColliderComponent가 보여 준다 — 정보 구조체를 반영하지
   // 않고 컴포넌트에 미러 필드(staticFriction·dynamicFriction·restitution·density)를
   // 두어 그것만 저장하고 런타임에 m_Info로 동기화한다. MeshCollider가 m_Info를
   // 직접 반영한 것이 예외였다.
   //
   // ★ 남은 일: 같은 미러 필드를 여기에도 세우면 메시 콜라이더의 마찰·반발·밀도·
   //   convexPolygonLimit이 처음으로 저장된다. 그건 저장 포맷이 늘어나는 별도
   //   작업이라 C1에서 하지 않는다(현재 저장 인스턴스 0건이라 마이그레이션 위험은 없다).
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_posOffset>,
           meta::field<&Self::m_rotOffset>);
   }
public:
	MeshColliderComponent() = default;
	
	void OnInitialized() override
	{
		auto scene = GetOwner()->m_ownerScene;
		if (scene)
		{
			scene->CollectColliderComponent(this);
		}
	}

	void OnUninitializing() override
	{
		auto scene = GetOwner()->m_ownerScene;
		if (scene)
		{
			scene->UnCollectColliderComponent(this);
		}
	}
	
	//info
	float GetStaticFriction() const
	{
		return m_Info.colliderInfo.staticFriction;
	}
	void SetStaticFriction(float staticFriction)
	{
		m_Info.colliderInfo.staticFriction = staticFriction;
	}
	float GetDynamicFriction() const
	{
		return m_Info.colliderInfo.dynamicFriction;
	}
	void SetDynamicFriction(float dynamicFriction)
	{
		m_Info.colliderInfo.dynamicFriction = dynamicFriction;
	}
	float GetRestitution() const
	{
		return m_Info.colliderInfo.restitution;
	}
	void SetRestitution(float restitution)
	{
		m_Info.colliderInfo.restitution = restitution;
	}
	float GetDensity() const
	{
		return m_Info.colliderInfo.density;
	}
	void SetDensity(float density)
	{
		m_Info.colliderInfo.density = density;
	}
	

	unsigned int GetCollisionCount() const
	{
		return m_collsionCount;
	}



	//ConvexMesh
	unsigned char GetMeshPolygonLimit() const
	{
		return m_Info.convexPolygonLimit;
	}

	void SetMeshPolygonLimit(unsigned char polygonLimit)
	{
		m_Info.convexPolygonLimit = polygonLimit;
	}

	ConvexMeshColliderInfo GetMeshInfo() const
	{
		return m_Info;
	}
	void SetMeshInfoMation(const ConvexMeshColliderInfo& info)
	{
		m_Info = info;
	}




	//=========================================================
	// ICollider��(��) ���� ��ӵ�
	void SetPositionOffset(math::vector3 pos) override {
		m_posOffset = pos;
	}
	math::vector3 GetPositionOffset() override {
		return m_posOffset;
	}
	void SetRotationOffset(math::quaternion rotation) override {
		m_rotOffset = rotation;
	}
	math::quaternion GetRotationOffset() override {
		return m_rotOffset;
	}

	void OnTriggerEnter(ICollider* other) override {
		++m_collsionCount;
	}
	void OnTriggerStay(ICollider* other) override {
	}
	void OnTriggerExit(ICollider* other) override {
		if (m_collsionCount != 0) {
			--m_collsionCount;
		}
	}
	void OnCollisionEnter(ICollider* other) override {
		++m_collsionCount;
	}
	void OnCollisionStay(ICollider* other) override {

	}
	void OnCollisionExit(ICollider* other) override {
		if (m_collsionCount != 0) {
			--m_collsionCount;
		}
	}
	void SetColliderType(EColliderType type) override {
		m_type = type;
	}
	EColliderType GetColliderType() const override {
		return m_type;
	}

public:
	ConvexMeshColliderInfo m_Info;
	math::vector3 m_posOffset{};
private:
	EColliderType m_type;
public:
	math::quaternion m_rotOffset{};
private:
	unsigned int m_collsionCount = 0;
};
