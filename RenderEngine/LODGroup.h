#pragma once
#include "MeshOptimizer.h"
#include "LODSetting.h"

class LODGroup
{
public:
	LODGroup(Mesh* baseMesh);
	~LODGroup();

	// 외부에서 수동 호출 시
	void UpdateLODs();
	Mesh* GetLOD(size_t level) const;
	Mesh* GetBaseMesh() const { return m_baseMesh; }
	Mesh* GetLODByDistance(const float& distance) const;

private:
	void DefaultThresholds();

private:
	Mesh* m_baseMesh = nullptr;
	std::vector<Mesh*> m_lods;
	std::vector<LODThreshold> m_lodThresholds;
};