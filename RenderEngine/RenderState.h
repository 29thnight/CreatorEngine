#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"

class RenderPerformanceState : public Singleton<RenderPerformanceState>
{
private:
	friend class Singleton;
	RenderPerformanceState() = default;
	~RenderPerformanceState() = default;

public:
	void UpdateRenderState(const std::string& name, double value)
	{
		m_renderStates[name] = value;
	}

	double GetRenderState(const std::string& name)
	{
		auto it = m_renderStates.find(name);
		if (it != m_renderStates.end())
		{
			return it->second;
		}
		return 0.0; // Default value if not found
	}

private:
	std::unordered_map<std::string, double> m_renderStates;
};

static auto& RenderStatistics = RenderPerformanceState::GetInstance();
#endif // !DYNAMICCPP_EXPORTS