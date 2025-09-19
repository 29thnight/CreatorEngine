#pragma once

#define ReflectInputDeviceDetector \
ReflectionScriptField(InputDeviceDetector) \
{ \
	MethodField \
	({ \
		meta_method(MoveSelector, "dir") \
	}); \
	FieldEnd(InputDeviceDetector, MethodOnly) \
};
