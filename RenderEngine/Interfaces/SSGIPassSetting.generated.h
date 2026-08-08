#pragma once

#define ReflectSSGIPassSetting \
ReflectionField(SSGIPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(isOn) \
		meta_property(useOnlySSGI) \
		meta_property(useDualFilteringStep) \
		meta_property(radius) \
		meta_property(thickness) \
		meta_property(intensity) \
		meta_property(ssratio) \
	}); \
	FieldEnd(SSGIPassSetting, PropertyOnly) \
};
