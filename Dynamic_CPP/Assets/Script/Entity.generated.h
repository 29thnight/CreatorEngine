#pragma once

#define ReflectEntity \
ReflectionScriptField(Entity) \
{ \
	PropertyField \
	({ \
		meta_property(m_maxHP) \
	}); \
	FieldEnd(Entity, PropertyOnly) \
};
