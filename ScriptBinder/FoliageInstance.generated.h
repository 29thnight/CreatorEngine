#pragma once

#define ReflectFoliageInstance \
ReflectionField(FoliageInstance) \
{ \
	PropertyField \
	({ \
		meta_property(m_position) \
		meta_property(m_rotation) \
		meta_property(m_scale) \
		meta_property(m_foliageTypeID) \
	}); \
	FieldEnd(FoliageInstance, PropertyOnly) \
};
