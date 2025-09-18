#pragma once

#define ReflectMovingUILayer \
ReflectionScriptField(MovingUILayer) \
{ \
	PropertyField \
	({ \
		meta_property(m_movingSpeed) \
		meta_property(m_waitTick) \
		meta_property(m_baseY) \
		meta_property(offset) \
	}); \
	FieldEnd(MovingUILayer, PropertyOnly) \
};
