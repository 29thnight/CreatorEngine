#pragma once

#define ReflectFoliageComponent \
ReflectionFieldInheritance(FoliageComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_foliageAssetGuid) \
		meta_property(m_foliageTypes) \
		meta_property(m_foliageInstances) \
	}); \
	FieldEnd(FoliageComponent, PropertyOnlyInheritance) \
};
