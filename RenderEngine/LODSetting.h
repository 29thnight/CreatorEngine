#pragma once
#include "Core.Minimal.h"

struct LODThreshold
{
	float minDistance; // LOD�� ���۵Ǵ� �ּ� �Ÿ�
	float maxDistance; // �ش� LOD�� ������ �ִ� �Ÿ�
};

class Mesh;
class LODGroup;
namespace LODSettings
{
	static inline std::atomic<bool>	g_EnableLOD = false;
	static inline std::atomic<float> g_LODReductionRatio = 0.5f;
	static inline std::atomic<size_t> g_MaxLODLevels = 2;

	extern void SetLODReductionRatio(float ratio);
	extern void RegisterLODGroup(class LODGroup* group);
	extern void UnregisterLODGroup(class LODGroup* group);
	inline bool IsLODEnabled() { return g_EnableLOD.load(); }
}
