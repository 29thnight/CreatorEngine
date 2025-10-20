#pragma once

#define ReflectControllerVibration \
ReflectionScriptField(ControllerVibration) \
{ \
	PropertyField \
	({ \
		meta_property(PlayerHitPower) \
		meta_property(PlayerHitTime) \
		meta_property(PlayerChargePower) \
		meta_property(PlayerChargeTime) \
	}); \
	FieldEnd(ControllerVibration, PropertyOnly) \
};
