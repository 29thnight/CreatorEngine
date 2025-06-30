#pragma once

#define ReflectTerrainComponent \
ReflectionFieldInheritance(TerrainComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_width) \
		meta_property(m_height) \
		meta_property(m_trrainAssetGuid) \
	}); \
	FieldEnd(TerrainComponent, PropertyOnlyInheritance) \
};
