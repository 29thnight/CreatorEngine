#pragma once

#define ReflectEntityEnemy \
ReflectionScriptField(EntityEnemy) \
{ \
	PropertyField \
	({ \
		meta_property(m_knockBackVelocity) \
		meta_property(m_knockBackScaleVelocity) \
		meta_property(m_MaxknockBackTime) \
	}); \
	FieldEnd(EntityEnemy, PropertyOnly) \
};
