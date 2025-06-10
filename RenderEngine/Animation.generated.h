#pragma once

#define ReflectAnimation \
ReflectionField(Animation) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(m_isLoop) \
	}); \
	FieldEnd(Animation, PropertyOnly) \
};
