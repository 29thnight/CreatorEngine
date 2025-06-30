#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/Core.Thread.hpp"

class AssetLoadJob
{
public:
	AssetLoadJob();
	~AssetLoadJob();

	ThreadPool<std::function<void()>> m_AssetLoadThreadPool;
};
#endif // !DYNAMICCPP_EXPORTS