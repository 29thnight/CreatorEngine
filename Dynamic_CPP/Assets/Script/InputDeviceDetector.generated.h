#pragma once

#define ReflectInputDeviceDetector \
ReflectionScriptField(InputDeviceDetector) \
{ \
	PropertyField \
	({ \
		meta_property(m_playerIndex) \
		meta_property(m_lastDelta) \
	}); \
	MethodField \
	({ \
		meta_method(SetSlot, "slot") \
		meta_method(MoveSelector, "dir") \
	}); \
	FieldEnd(InputDeviceDetector, PropertyAndMethod) \
};
