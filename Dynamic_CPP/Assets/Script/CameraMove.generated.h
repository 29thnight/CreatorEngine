#pragma once

#define ReflectCameraMove \
ReflectionScriptField(CameraMove) \
{ \
	PropertyField \
	({ \
		meta_property(followSpeed) \
		meta_property(offset) \
	}); \
	FieldEnd(CameraMove, PropertyOnly) \
};
