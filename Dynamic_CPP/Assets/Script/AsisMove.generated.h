#pragma once

#define ReflectAsisMove \
ReflectionScriptField(AsisMove) \
{ \
	PropertyField \
	({ \
		meta_property(moveSpeed) \
		meta_property(pathRadius) \
		meta_property(predictNextTime) \
		meta_property(rotateSpeed) \
	}); \
	FieldEnd(AsisMove, PropertyOnly) \
};
