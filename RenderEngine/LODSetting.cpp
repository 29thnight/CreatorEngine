#include "LODSetting.h"
#include "LODGroup.h"

namespace LODSettings
{
	static std::vector<LODGroup*> g_registeredGroups;

	void SetLODReductionRatio(float ratio)
	{
		g_LODReductionRatio.store(ratio);
		for (LODGroup* group : g_registeredGroups)
		{
			if (group)
			{
				group->UpdateLODs();
			}
		}
	}

	void RegisterLODGroup(LODGroup* group)
	{
		if (group)
		{
			g_registeredGroups.push_back(group);
		}
	}

	void UnregisterLODGroup(LODGroup* group)
	{
		if (group)
		{
			std::erase_if(g_registeredGroups, [group](LODGroup* g) { return g == group; });
		}
	}
}