#pragma once

#define ReflectIllustrationMove \
ReflectionScriptField(IllustrationMove) \
{ \
	PropertyField \
	({ \
		meta_property(m_movingSpeed) \
		meta_property(m_waitTick) \
		meta_property(m_baseY) \
		meta_property(offset) \
	}); \
	FieldEnd(IllustrationMove, PropertyOnly) \
};
