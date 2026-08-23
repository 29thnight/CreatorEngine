#pragma once
#include "Physx.h"

struct ColliderDebugData
{
	PxTransform shapeWorldTransform;

	static ColliderDebugData GetDebugData(uint32_t instanceID)
	{
		auto ptr = Physics->GetRigidBody(instanceID);

		StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(ptr);

		if (staticBody)
		{
			ColliderDebugData debugData;
			auto pxStaticBody = staticBody->GetRigidStatic();

			PxTransform actorWorldPose = pxStaticBody->getGlobalPose();
			uint32_t shapeIndex = pxStaticBody->getNbShapes();
			PxShape* shape[64]{};

			uint32_t shapeCount = pxStaticBody->getShapes(shape, shapeIndex);

			std::vector<PxShape*> shapes(shape, shape + shapeCount);

			for (auto& shape : shapes)
			{
				auto& geometry = shape->getGeometry();
				if (geometry.getType() == PxGeometryType::eBOX)
				{
					const PxBoxGeometry& boxGeometry = 
						static_cast<const PxBoxGeometry&>(geometry);
				}
			}

			PxTransform shapeLocalPose = {};

			return debugData;
		}

		throw std::runtime_error("ColliderDebugData::GetDebugData : Invalid instanceID or not a StaticRigidBody");
	}
};