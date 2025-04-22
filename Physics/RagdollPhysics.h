#pragma once
#include <physx/PxPhysicsAPI.h>
#include "PhysicsCommon.h"
#include "RagdollLink.h"

class RagdollPhysics
{
public:
	RagdollPhysics();
	~RagdollPhysics();

	void Initialize(const ArticulationInfo& info,physx::PxPhysics* physics,CollisionData* collisionData);
	void Update(float deltaTime);
	
	bool AddArticulationLink(const LinkInfo& linkInfo, int* collisionMatrix,const DirectX::SimpleMath::Vector3& extend);
	bool AddArticulationLink(const LinkInfo& linkInfo, int* collisionMatrix, const float& radius);
	bool AddArticulationLink(const LinkInfo& linkInfo, int* collisionMatrix, const float& halfHeight,const float& radius);
	bool AddArticulationLink(LinkInfo& linkInfo, int* collisionMatrix);

	bool ChangeLayerNumber(const unsigned int& newLayerNumber, int* collisionMatrix);

	
	inline const std::string& GetModelPath() const { return m_modelPath; }
	inline const unsigned int& GetID() const { return m_id; }
	inline const unsigned int& GetLayerNumber() const { return m_layerNumber; }
	inline const bool& GetIsRagdoll() const { return m_bIsRagdoll; }
	inline const RagdollLink* GetRootLink() const { return m_rootLink; }
	inline const RagdollLink* FindLink(std::string name) { m_linkContainer[name]; }
	inline const DirectX::SimpleMath::Matrix& GetWorldTransform() const { return m_worldTransform; }
	inline physx::PxArticulationReducedCoordinate* GetPxArticulation() { return m_pxArticulation; }
	inline const std::unordered_map<std::string, RagdollLink*>& GetLinkContainer() const { return m_linkContainer; } 
	
	inline void SetIsRagdoll(const bool& isRagdoll) { m_bIsRagdoll = isRagdoll; }
	void SetWorldTransform(const DirectX::SimpleMath::Matrix& worldTransform);
	bool SetLinkTransformUpdate(const std::string& name, const DirectX::SimpleMath::Matrix& boneWorldTransform);


private:
	std::string m_modelPath;
	unsigned int m_id;
	unsigned int m_layerNumber;
	bool m_bIsRagdoll;

	physx::PxMaterial* m_material; //물리 재질

	RagdollLink* m_rootLink;
	CollisionData* m_collisionData;

	std::unordered_map<std::string, RagdollLink*> m_linkContainer; //링크 관리용
	DirectX::SimpleMath::Matrix m_worldTransform; //관절 루트 트렌스폼

	physx::PxArticulationReducedCoordinate* m_pxArticulation; //관절


};

