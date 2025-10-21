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
		meta_property(PlayerChargeEndPower) \
		meta_property(PlayerChargeEndTime) \
	}); \
	FieldEnd(ControllerVibration, PropertyOnly) \
};
