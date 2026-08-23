#include "HeightFieldResource.h"
#include <vector>

HeightFieldResource::HeightFieldResource(physx::PxPhysics* physics, const float* height, const unsigned int& numCols, const unsigned int& numRows) : ResourceBase(EResourceType::HEIGHT_FIELD)
{
	//height field 데이터 생성
	std::vector<physx::PxHeightFieldSample> samples(numRows * numCols);
	float worldHeightRange = 500.0f - (-100.0f);
	float calculatedHeightScale = worldHeightRange / 32767.0f;
	//샘플 데이터 입력(높이값 입력)
	for (physx::PxU32 i = 0; i < numRows; ++i) {
		for (physx::PxU32 j = 0; j < numCols; ++j) {
			float currentWorldHeight = height[i * numCols + j];
			physx::PxI16 pxHeightValue = (physx::PxI16)((currentWorldHeight - (-100.0f)) / calculatedHeightScale);
			pxHeightValue = physx::PxClamp(pxHeightValue, (physx::PxI16)-32768, (physx::PxI16)32767);
			samples[j * numRows + i].height = pxHeightValue;
			samples[j * numRows + i].materialIndex0 = 0; // 기본 재질 인덱스
			samples[j * numRows + i].materialIndex1 = 0;
			samples[j * numRows + i].setTessFlag();
			
		}
	}

	//descrictor
	physx::PxHeightFieldDesc heightFieldDesc;
	heightFieldDesc.format = physx::PxHeightFieldFormat::eS16_TM;
	heightFieldDesc.nbRows = numCols; 
	heightFieldDesc.nbColumns = numRows;
	heightFieldDesc.samples.data = samples.data();
	heightFieldDesc.samples.stride = sizeof(physx::PxHeightFieldSample);

	//height field 생성
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
