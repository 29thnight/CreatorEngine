#pragma once

#define ReflectTBoss1 \
ReflectionScriptField(TBoss1) \
{ \
	PropertyField \
	({ \
		meta_property(m_MaxHp) \
		meta_property(BP001Damage) \
		meta_property(BP002Damage) \
		meta_property(BP003Damage) \
		meta_property(BP001RadiusSize) \
		meta_property(BP002Dist) \
		meta_property(BP003RadiusSize) \
		meta_property(MoveSpeed) \
		meta_property(BP003Delay) \
	}); \
	MethodField \
	({ \
		meta_method(Burrow) \
		meta_method(SetBurrow) \
		meta_method(Protrude) \
		meta_method(ProtrudeEnd) \
		meta_method(ProtrudeChunsik) \
		meta_method(ShootProjectile) \
		meta_method(SweepAttack) \
		meta_method(BP0011) \
		meta_method(BP0012) \
		meta_method(BP0013) \
		meta_method(BP0014) \
		meta_method(BP0021) \
		meta_method(BP0022) \
		meta_method(BP0031) \
		meta_method(BP0032) \
		meta_method(BP0033) \
		meta_method(BP0034) \
	}); \
	FieldEnd(TBoss1, PropertyAndMethod) \
};
