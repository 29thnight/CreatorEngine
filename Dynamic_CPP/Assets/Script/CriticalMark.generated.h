#pragma once

#define ReflectCriticalMark \
ReflectionScriptField(CriticalMark) \
{ \
	PropertyField \
	({ \
		meta_property(markDuration) \
		meta_property(markCoolDown) \
	}); \
	FieldEnd(CriticalMark, PropertyOnly) \
};
