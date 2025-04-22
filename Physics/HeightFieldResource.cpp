#include "HeightFieldResource.h"
#include <vector>

HeightFieldResource::HeightFieldResource(physx::PxPhysics* physics, const int* height, const unsigned int& numCols, const unsigned int& numRows) : ResourceBase(EResourceType::HEIGHT_FIELD)
{
	//height field ������ ����
	std::vector<physx::PxHeightFieldSample> samples(numRows * numCols);

	//���� ������ �Է�(���̰� �Է�)
	for (physx::PxU32 i = 1; i < numRows; ++i) {
		for (physx::PxU32 j = 1; j < numCols; ++j) {
			
			samples[(numRows - i) * numCols - j].height = -height[(numRows - i) * numCols - j];
			samples[(numRows - i) * numCols - j].setTessFlag();
			
		}
	}

	//descrictor
	physx::PxHeightFieldDesc heightFieldDesc;
	heightFieldDesc.format = physx::PxHeightFieldFormat::eS16_TM;
	heightFieldDesc.nbRows = numRows;
	heightFieldDesc.nbColumns = numCols;
	heightFieldDesc.samples.data = samples.data();
	heightFieldDesc.samples.stride = sizeof(physx::PxHeightFieldSample);

	//height field ����
	m_heightField = PxCreateHeightField(heightFieldDesc, physics->getPhysicsInsertionCallback());
}

HeightFieldResource::~HeightFieldResource()
{
	if (m_heightField != nullptr)
	{
		m_heightField->release();
		m_heightField = nullptr;
	}
}
