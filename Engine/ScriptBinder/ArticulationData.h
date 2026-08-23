#pragma once
#include "../Physics/PhysicsCommon.h"
#include "LinkData.h"



class LinkData;
class ArticulationData {
public:
	ArticulationData() = default;
	~ArticulationData() = default;
	
	void Update() {
		m_rootLinkData->Update(m_articulationInfo.worldTransform);
	}

	inline const unsigned int& GetID() {	return m_articulationInfo.id; }
	inline void SetID(unsigned int& id) { m_articulationInfo.id = id; }

	inline const unsigned int& GetLayerNumber() {	return m_articulationInfo.layerNumber;}
	inline void SetLayerNumber(unsigned int& layerNumber) {m_articulationInfo.layerNumber = layerNumber;}

	inline LinkData* GetRootLinkData() { return m_rootLinkData; }
	inline void SetRootLinkData(LinkData* rootLinkData) { m_rootLinkData = rootLinkData; }

	inline const DirectX::SimpleMath::Matrix& GetWorldTransform() {	return m_articulationInfo.worldTransform;}
	inline void SetWorldTransform(const DirectX::SimpleMath::Matrix& worldTransform) {	m_articulationInfo.worldTransform = worldTransform;}

	/*inline const DirectX::SimpleMath::Matrix& GetWorldTransform() const { return m_articulationInfo.worldTransform; }
	inline void SetWorldTransform(const DirectX::SimpleMath::Matrix& worldTransform) { m_articulationInfo.worldTransform = worldTransform; }*/

	inline const float& GetStaticFriction() const { return m_articulationInfo.staticFriction; }
	inline void SetStaticFriction(const float& staticFriction) { m_articulationInfo.staticFriction = staticFriction; }

	inline const float& GetDynamicFriction() const { return m_articulationInfo.dynamicFriction; }
	inline void SetDynamicFriction(const float& dynamicFriction) { m_articulationInfo.dynamicFriction = dynamicFriction; }

	inline const float& GetRestitution() const { return m_articulationInfo.restitution; }
	inline void SetRestitution(const float& restitution) { m_articulationInfo.restitution = restitution; }

	inline const float& GetDensity() const { return m_articulationInfo.density; }
	inline void SetDensity(const float& density) { m_articulationInfo.density = density; }


private:
	ArticulationInfo m_articulationInfo;
	LinkData* m_rootLinkData;
};