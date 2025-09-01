#pragma once

#define ReflectNormalBullet \
ReflectionScriptField(NormalBullet) \
{ \
	PropertyField \
	({ \
		meta_property(rangedProjSpd) \
		meta_property(rangedProjDist) \
	}); \
	FieldEnd(NormalBullet, PropertyOnly) \
};
