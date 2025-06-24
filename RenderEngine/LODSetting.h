#pragma once
#include "Core.Minimal.h"

struct LODThreshold
{
	float minDistance; // LOD가 시작되는 최소 거리
	float maxDistance; // 해당 LOD를 유지할 최대 거리
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
