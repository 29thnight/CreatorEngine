#pragma once
#include "../Physics/PhysicsCommon.h"


enum class EShapeType {
	BOX,
	SPHERE,
	CAPSULE,

	END
};

class LinkData
{
public:
	LinkData() = default;
	~LinkData() = default;

	void Update(const DirectX::SimpleMath::Matrix& ParentWorldTransForm) {
		m_parentWorldTransform = ParentWorldTransForm;
		m_worldTransform = m_linkInfo.localTransform * m_parentWorldTransform;
		m_linkInfo.worldTransform = m_linkInfo.localTransform * m_parentWorldTransform;

		for (auto it = m_childLinkData.begin(); it != m_childLinkData.end();) {
			if (it->second->GetIsDead()) {
				it = m_childLinkData.erase(it);
			}
			else {
				it->second->Update(m_worldTransform);
				++it;
			}
		}
	}

	inline void SetID(const int& id) { m_ID = id; }
	inline const unsigned int& GetID() const { return m_ID; }

	inline void SetIsDead(const bool& isDead) { m_isDead = isDead; }
	inline const bool& GetIsDead() const { return m_isDead; }

	inline std::unordered_map<std::string, LinkData*>& GetChildrenLinkData() { return m_childLinkData; }
	inline LinkData* GetChildLinkData(const std::string& name) {
		auto iter = m_childLinkData.find(name);
		if (iter != m_childLinkData.end()) {
			return iter->second;
		}
		return nullptr;
	}
	inline void AddChildLinkData(const std::string& name, LinkData* linkData) {
		m_childLinkData.insert(std::make_pair(name, linkData));
	}
	inline void RemoveChildLinkData(const std::string& name) {
		auto iter = m_childLinkData.find(name);
		if (iter != m_childLinkData.end()) {
			m_childLinkData.erase(iter);
		}
	}

	//link
	inline void SetBoneName(const std::string& name) { m_linkInfo.boneName = name; }
	inline const std::string& GetBoneName() const { return m_linkInfo.boneName; }
	inline void SetParentBoneName(const std::string& name) { m_linkInfo.parentBoneName = name; }
	inline const std::string& GetParentBoneName() const { return m_linkInfo.parentBoneName; }
	inline void SetDensity(const float& density) { m_linkInfo.density = density; }
	inline const float& GetDensity() const { return m_linkInfo.density; }
	inline void SetLocalTransform(const DirectX::SimpleMath::Matrix& localTransform) { m_linkInfo.localTransform = localTransform; }
	inline const DirectX::SimpleMath::Matrix& GetLocalTransform() const { return m_linkInfo.localTransform; }
	inline void SetWorldTransform(const DirectX::SimpleMath::Matrix& worldTransform) { m_linkInfo.worldTransform = worldTransform; }
	inline const DirectX::SimpleMath::Matrix& GetWorldTransform() const { return m_linkInfo.worldTransform; }
	inline const DirectX::SimpleMath::Matrix& GetParenttransform() const { return m_parentWorldTransform; }

	//shape
	inline void SetShapeType(const EShapeType& shapeType) { m_shapeType = shapeType; }
	inline const EShapeType& GetShapeType() const { return m_shapeType; }
	inline void SetBoxExtent(const DirectX::SimpleMath::Vector3& extent) { m_extent = extent; }
	inline const DirectX::SimpleMath::Vector3& GetBoxExtent() const { return m_extent; }
	inline void SetSphereRadius(const float& radius) { m_sphereRadius = radius; }
	inline const float& GetSphereRadius() const { return m_sphereRadius; }
	inline void SetCapsuleRadius(const float& radius) { m_capsuleRadius = radius; }
	inline const float& GetCapsuleRadius() const { return m_capsuleRadius; }
	inline void SetCapsuleHalfHeight(const float& halfHeight) { m_capsuleHalfHeight = halfHeight; }
	inline const float& GetCapsuleHalfHeight() const { return m_capsuleHalfHeight; }
	
	//joint
	inline void SetJointLocalTransform(const DirectX::SimpleMath::Matrix& localTransform) { m_linkInfo.jointInfo.localTransform = localTransform; }
	inline void SetJointLocalTransform(const DirectX::SimpleMath::Vector3& position, const DirectX::SimpleMath::Quaternion& rotation) {
		m_linkInfo.jointInfo.localTransform = DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) * DirectX::SimpleMath::Matrix::CreateTranslation(position);
	}
	inline const DirectX::SimpleMath::Matrix& GetJointLocalTransform() const { return m_linkInfo.jointInfo.localTransform; }
	inline void SetJointStiffness(const float& stiffness) { m_linkInfo.jointInfo.stiffness = stiffness; }
	inline const float& GetJointStiffness() const { return m_linkInfo.jointInfo.stiffness; }
	inline void SetJointDamping(const float& damping) { m_linkInfo.jointInfo.damping = damping; }
	inline const float& GetJointDamping() const { return m_linkInfo.jointInfo.damping; }
	inline void SetJointMaxForce(const float& maxForce) { m_linkInfo.jointInfo.maxForce = maxForce; }
	inline const float& GetJointMaxForce() const { return m_linkInfo.jointInfo.maxForce; }

	//joint axis
	inline void SetXAxisMotion(const EArticulationMotion& motion) { m_linkInfo.jointInfo.xAxisInfo.motion = motion; }
	inline const EArticulationMotion& GetXAxisMotion() const { return m_linkInfo.jointInfo.xAxisInfo.motion; }
	inline void SetXAxisLimitLow(const float& limit) { m_linkInfo.jointInfo.xAxisInfo.limitlow = limit; }
	inline const float& GetXAxisLimitLow() const { return m_linkInfo.jointInfo.xAxisInfo.limitlow; }
	inline void SetXAxisLimitHigh(const float& limit) { m_linkInfo.jointInfo.xAxisInfo.limitHigh = limit; }
	inline const float& GetXAxisLimitHigh() const { return m_linkInfo.jointInfo.xAxisInfo.limitHigh; }
	inline void SetYAxisMotion(const EArticulationMotion& motion) { m_linkInfo.jointInfo.yAxisInfo.motion = motion; }
	inline const EArticulationMotion& GetYAxisMotion() const { return m_linkInfo.jointInfo.yAxisInfo.motion; }
	inline void SetYAxisLimitLow(const float& limit) { m_linkInfo.jointInfo.yAxisInfo.limitlow = limit; }
	inline const float& GetYAxisLimitLow() const { return m_linkInfo.jointInfo.yAxisInfo.limitlow; }
	inline void SetYAxisLimitHigh(const float& limit) { m_linkInfo.jointInfo.yAxisInfo.limitHigh = limit; }
	inline const float& GetYAxisLimitHigh() const { return m_linkInfo.jointInfo.yAxisInfo.limitHigh; }
	inline void SetZAxisMotion(const EArticulationMotion& motion) { m_linkInfo.jointInfo.zAxisInfo.motion = motion; }
	inline const EArticulationMotion& GetZAxisMotion() const { return m_linkInfo.jointInfo.zAxisInfo.motion; }
	inline void SetZAxisLimitLow(const float& limit) { m_linkInfo.jointInfo.zAxisInfo.limitlow = limit; }
	inline const float& GetZAxisLimitLow() const { return m_linkInfo.jointInfo.zAxisInfo.limitlow; }
	inline void SetZAxisLimitHigh(const float& limit) { m_linkInfo.jointInfo.zAxisInfo.limitHigh = limit; }
	inline const float& GetZAxisLimitHigh() const { return m_linkInfo.jointInfo.zAxisInfo.limitHigh; }

private:

	unsigned int m_ID;
	LinkInfo m_linkInfo;
	bool m_isDead;
	
	DirectX::SimpleMath::Matrix m_parentWorldTransform;
	DirectX::SimpleMath::Matrix m_worldTransform;
	DirectX::SimpleMath::Vector3 m_extent;

	EShapeType m_shapeType;
	float m_sphereRadius;
	float m_capsuleRadius;
	float m_capsuleHalfHeight;

	std::unordered_map<std::string, LinkData*> m_childLinkData;
};