#include "LODGroup.h"
#include "ResourceAllocator.h"

LODGroup::LODGroup(Mesh* baseMesh)
	: m_baseMesh(baseMesh)
{
	LODSettings::RegisterLODGroup(this);
	DefaultThresholds();
	UpdateLODs(); // 초기 생성
}

LODGroup::~LODGroup()
{
	LODSettings::UnregisterLODGroup(this);

	for (size_t i = 1; i < m_lods.size(); ++i)
		DeallocateResource(m_lods[i]); // 리소스 풀에서 해제
	m_lods.clear();
}

void LODGroup::UpdateLODs()
{
	// 이전 LOD 메시 삭제
	for (size_t i = 1; i < m_lods.size(); ++i)
		DeallocateResource(m_lods[i]); // 리소스 풀에서 해제
	m_lods.clear();

	// LOD0은 원본 메시
	MeshOptimizer::GenerateLODs(m_lods, m_baseMesh, LODSettings::g_MaxLODLevels, LODSettings::g_LODReductionRatio);
}

Mesh* LODGroup::GetLOD(size_t level) const
{
	if (level >= m_lods.size())
		return m_lods[m_lods.size() - 1];
	return m_lods[level];
}

void LODGroup::DefaultThresholds()
{
	constexpr float base = 30.0f;
	for (size_t i = 0; i < LODSettings::g_MaxLODLevels; ++i)
	{
		m_lodThresholds.push_back({
			base * std::pow(2.0f, static_cast<float>(i)),
			base * std::pow(2.0f, static_cast<float>(i + 1))
		});
	}
}

Mesh* LODGroup::GetLODByDistance(const float& distance) const
{
	for (size_t i = 0; i < m_lodThresholds.size(); ++i)
	{
		const auto& th = m_lodThresholds[i];
		if (distance >= th.minDistance && distance < th.maxDistance)
		{
			return GetLOD(i + 1);
		}
	}

	// 거리가 최소 LOD 거리보다 작으면 첫 번째 LOD
	if (distance < m_lodThresholds[0].minDistance)
	{
		return GetLOD(0);
	}

	// 초과 거리 → 마지막 LOD
	return GetLOD(m_lods.size() - 1);
}