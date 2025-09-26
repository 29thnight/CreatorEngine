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
		meta_property(BP001Dist) \
		meta_property(BP002RadiusSize) \
		meta_property(BP003RadiusSize) \
		meta_property(MoveSpeed) \
		meta_property(BP003Delay) \
	}); \
	MethodField \
	({ \
		meta_method(BP0031) \
		meta_method(BP0032) \
		meta_method(BP0033) \
		meta_method(BP0034) \
	}); \
	FieldEnd(TBoss1, PropertyAndMethod) \
};
