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
		meta_property(PlayerChargeHitPower) \
		meta_property(PlayerChargeHitTime) \
		meta_property(BombExplosionPower) \
		meta_property(BombExplosionTime) \
		meta_property(EleteKillPower) \
		meta_property(EleteKillTime) \
		meta_property(GateDestroyPower) \
		meta_property(GateDestroyTime) \
		meta_property(BossKillPower) \
		meta_property(BossKillTime) \
		meta_property(PlayerAllStunPower) \
		meta_property(PlayerAllStunTime) \
		meta_property(KoriStunPower) \
		meta_property(KoriStunTime) \
	}); \
	FieldEnd(ControllerVibration, PropertyOnly) \
};
