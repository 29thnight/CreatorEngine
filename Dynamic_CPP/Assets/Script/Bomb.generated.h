#pragma once

#define ReflectBomb \
ReflectionScriptField(Bomb) \
{ \
	PropertyField \
	({ \
		meta_property(throwSpeed) \
		meta_property(m_throwPowerY) \
		meta_property(duration) \
	}); \
	FieldEnd(Bomb, PropertyOnly) \
};
