#pragma once

#define ReflectTerrainComponent \
ReflectionFieldInheritance(TerrainComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_width) \
		meta_property(m_height) \
	}); \
	FieldEnd(TerrainComponent, PropertyOnlyInheritance) \
};
