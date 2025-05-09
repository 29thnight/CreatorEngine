#pragma once

#define ReflectTerrain \
ReflectionFieldInheritance(Terrain, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_width) \
		meta_property(m_height) \
	}); \
	FieldEnd(Terrain, PropertyOnlyInheritance) \
};
