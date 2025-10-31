#pragma once

#define ReflectCameraMove \
ReflectionScriptField(CameraMove) \
{ \
	PropertyField \
	({ \
		meta_property(followSpeed) \
		meta_property(offset) \
		meta_property(detectRange) \
		meta_property(cameraMoveSpeed) \
		meta_property(OnCaculCamera) \
	}); \
	MethodField \
	({ \
		meta_method(OnCameraControll) \
		meta_method(OffCameraCOntroll) \
		meta_method(CameraMoveFun, "dir") \
		meta_method(ShakeCamera, "duration") \
		meta_method(ShakeCamera1s) \
	}); \
	FieldEnd(CameraMove, PropertyAndMethod) \
};
