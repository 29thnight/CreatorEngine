#pragma once

#define ReflectTerrainCollider \
ReflectionFieldInheritance(TerrainCollider, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_posOffset) \
	}); \
	FieldEnd(TerrainCollider, PropertyOnlyInheritance) \
};
