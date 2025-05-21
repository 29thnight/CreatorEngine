#pragma once

#define ReflectTerrainColliderComponent \
ReflectionFieldInheritance(TerrainColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_posOffset) \
	}); \
	FieldEnd(TerrainColliderComponent, PropertyOnlyInheritance) \
};
