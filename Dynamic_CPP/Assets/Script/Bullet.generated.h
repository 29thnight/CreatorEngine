#pragma once

#define ReflectBullet \
ReflectionScriptField(Bullet) \
{ \
	PropertyField \
	({ \
		meta_property(rangedProjSpd) \
		meta_property(rangedProjDist) \
	}); \
	FieldEnd(Bullet, PropertyOnly) \
};
