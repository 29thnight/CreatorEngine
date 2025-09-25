#pragma once

#define ReflectInputDeviceDetector \
ReflectionScriptField(InputDeviceDetector) \
{ \
	PropertyField \
	({ \
		meta_property(m_playerIndex) \
		meta_property(m_lastDelta) \
		meta_property(m_requiredSelectHold) \
		meta_property(m_requiredCancelHold) \
	}); \
	MethodField \
	({ \
		meta_method(MoveSelector, "dir") \
		meta_method(CharSelect) \
		meta_method(ReleaseKey) \
	}); \
	FieldEnd(InputDeviceDetector, PropertyAndMethod) \
};
