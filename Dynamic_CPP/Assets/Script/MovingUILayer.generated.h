#pragma once

#define ReflectMovingUILayer \
ReflectionScriptField(MovingUILayer) \
{ \
	PropertyField \
	({ \
		meta_property(playerIndex) \
	}); \
	FieldEnd(MovingUILayer, PropertyOnly) \
};
