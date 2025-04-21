#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include <physx/PxPhysicsAPI.h>
#include "PhysicsCommon.h"
#include "PhysicsHelper.h"
#include "RagdollJoint.h"



struct CollisionData;
struct LinkInfo;
class RagdollLink;
class RagdollJoint;

class RagdollLink
{
public:
	bool Initialize(const LinkInfo& linkInfo, RagdollLink* parentLink,physx::PxArticulationReducedCoordinate* pxArtriculation);
	bool Update();

	physx::PxShape* CreateShape(physx::PxMaterial* material, const Mathf::Vector3& extent, CollisionData* collisionData);
	physx::PxShape* CreateShape(physx::PxMaterial* material, const float& radius, const float& halfHeight, CollisionData* collisionData);
	physx::PxShape* CreateShape(physx::PxMaterial* material, const float& radius, CollisionData* collisionData);

	bool ChangeLayerNumber(const physx::PxFilterData& fillterData, CollisionData* collisionData);

	inline physx::PxArticulationLink* GetPxLink() { return m_pxLink; }
	inline const std::string& GetName() const { return m_name; }
	inline const Mathf::Matrix& GetLocalTransform() const { return m_localTransform; }
	inline const Mathf::Matrix& GetWorldTransform() const { return m_worldTransform; }
	inline const RagdollJoint* GetRagdollJoint() const { return m_myJoint; }
	inline const RagdollLink* GetParentLink() const { return m_parentLink; }
	inline const std::vector<RagdollLink*>& GetChildrenLinks() const { return m_childLink; }
	inline const RagdollLink* GetChildLink(std::string linkname) {
		for (const auto& child : m_childLink) {
			if (child->GetName() == linkname) {
				return child;
			}
		}
	}
	inline const void AddChildRagdollLink(RagdollLink* childLink) { m_childLink.push_back(childLink); }
	void SetWorldTransform(const Mathf::Matrix& worldTransform);

private:
	std::string m_name; //관절 이름
	float m_density; //관절 밀도
	Mathf::Matrix m_localTransform; //관절 로컬 트렌스폼
	Mathf::Matrix m_worldTransform; //관절 월드 트렌스폼
	physx::PxTransform m_pxLocalTransform; //관절 px 로컬 트렌스폼

	RagdollJoint* m_myJoint; //관절
	RagdollLink* m_parentLink; //부모 링크
	std::vector<RagdollLink*> m_childLink; //자식 링크

	physx::PxArticulationLink* m_pxLink; //관절 링크

};

