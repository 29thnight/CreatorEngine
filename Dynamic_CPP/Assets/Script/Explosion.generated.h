#pragma once

#define ReflectExplosion \
ReflectionScriptField(Explosion) \
{ \
	PropertyField \
	({ \
		meta_property(explosionRadius) \
	}); \
	FieldEnd(Explosion, PropertyOnly) \
};
