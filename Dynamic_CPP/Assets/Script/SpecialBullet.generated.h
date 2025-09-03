#pragma once

#define ReflectSpecialBullet \
ReflectionScriptField(SpecialBullet) \
{ \
	PropertyField \
	({ \
		meta_property(rangedProjSpd) \
		meta_property(rangedProjDist) \
		meta_property(explosionRadius) \
	}); \
	FieldEnd(SpecialBullet, PropertyOnly) \
};
