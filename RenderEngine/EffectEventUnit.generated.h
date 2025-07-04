#pragma once

#define ReflectEffectEventUnit \
ReflectionField(EffectEventUnit) \
{ \
	PropertyField \
	({ \
		meta_property(eventName) \
	}); \
	FieldEnd(EffectEventUnit, PropertyOnly) \
};
