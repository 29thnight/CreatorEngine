#pragma once

#define ReflectCameraMove \
ReflectionScriptField(CameraMove) \
{ \
	PropertyField \
	({ \
		meta_property(followSpeed) \
		meta_property(offset) \
		meta_property(detectRange) \
	}); \
	FieldEnd(CameraMove, PropertyOnly) \
};
