#pragma once
#include "Core.Minimal.h"

class Skeleton;
struct AnimatorData
{
	Skeleton* m_Skeleton{ nullptr };
	FileGuid m_Motion{};
};