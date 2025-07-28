#pragma once

#define ReflectFoliageType \
ReflectionField(FoliageType) \
{ \
	PropertyField \
	({ \
		meta_property(m_castShadow) \
		meta_property(m_isShadowRecive) \
		meta_property(m_modelName) \
	}); \
	FieldEnd(FoliageType, PropertyOnly) \
};
