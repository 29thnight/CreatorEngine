#pragma once

#define ReflectInputDeviceDetector \
ReflectionScriptField(InputDeviceDetector) \
{ \
	PropertyField \
	({ \
		meta_property(m_deadZone) \
		meta_property(m_repeatDelay) \
		meta_property(m_repeatRate) \
		meta_property(m_axisState) \
		meta_property(m_holdTime) \
		meta_property(m_repeatTime) \
		meta_property(m_lastDelta) \
	}); \
	MethodField \
	({ \
		meta_method(SetSlot, "slot") \
		meta_method(MoveSelector, "dir") \
	}); \
	FieldEnd(InputDeviceDetector, PropertyAndMethod) \
};
