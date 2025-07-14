#pragma once

#define ReflectAsisMove \
ReflectionScriptField(AsisMove) \
{ \
	PropertyField \
	({ \
		meta_property(m_moveSpeed) \
		meta_property(m_pathRadius) \
		meta_property(m_predictNextTime) \
		meta_property(m_rotateSpeed) \
	}); \
	FieldEnd(AsisMove, PropertyOnly) \
};
