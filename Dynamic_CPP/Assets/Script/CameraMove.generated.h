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
		meta_property(shakeDuration) \
		meta_property(shakeMagnitude) \
		meta_property(isBossCamera) \
	}); \
	MethodField \
	({ \
		meta_method(OnCameraControll) \
		meta_method(OffCameraCOntroll) \
		meta_method(CameraMoveFun, "dir") \
		meta_method(ShakeCamera1s) \
		meta_method(ShakeCamera, "duration", "magnitude") \
	}); \
	FieldEnd(CameraMove, PropertyAndMethod) \
};
