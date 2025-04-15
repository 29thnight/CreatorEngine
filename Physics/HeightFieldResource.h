#pragma once
#include <physx/PxPhysicsAPI.h>
#include "../Utility_Framework/Core.Minimal.h"
#include "ResourceBase.h"

class HeightFieldResource : public ResourceBase
{
public:
	HeightFieldResource(physx::PxPhysics* physics,const int* height,const unsigned int& numCols,const unsigned int& numRows);
	virtual ~HeightFieldResource();

	inline physx::PxHeightField* GetHeightField() const { return m_heightField; }
	
private:
	physx::PxHeightField* m_heightField;
};

