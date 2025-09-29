#pragma once

#define ReflectBomb \
ReflectionScriptField(Bomb) \
{ \
	PropertyField \
	({ \
		meta_property(m_throwPowerY) \
		meta_property(m_boundPowerY) \
	}); \
	FieldEnd(Bomb, PropertyOnly) \
};
