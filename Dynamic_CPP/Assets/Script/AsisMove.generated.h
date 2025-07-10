#pragma once

#define ReflectAsisMove \
ReflectionScriptField(AsisMove) \
{ \
	PropertyField \
	({ \
		meta_property(moveSpeed) \
		meta_property(currentPointIndex) \
	}); \
	FieldEnd(AsisMove, PropertyOnly) \
};
