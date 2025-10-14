#pragma once

#define ReflectSwordProjectile \
ReflectionScriptField(SwordProjectile) \
{ \
	PropertyField \
	({ \
		meta_property(rangedProjSpd) \
		meta_property(rangedProjDist) \
	}); \
	FieldEnd(SwordProjectile, PropertyOnly) \
};
