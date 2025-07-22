#pragma once

#define ReflectFoliageComponent \
ReflectionFieldInheritance(FoliageComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_foliageAssetGuid) \
	}); \
	FieldEnd(FoliageComponent, PropertyOnlyInheritance) \
};
