#pragma once

#define ReflectControllerVibration \
ReflectionScriptField(ControllerVibration) \
{ \
	PropertyField \
	({ \
		meta_property(PlayerHitPower) \
		meta_property(PlayerHitTime) \
	}); \
	FieldEnd(ControllerVibration, PropertyOnly) \
};
