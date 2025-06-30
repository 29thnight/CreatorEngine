#pragma once

#define ReflectEffectComponent \
ReflectionFieldInheritance(EffectComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(num) \
	}); \
	MethodField \
	({ \
		meta_method(PlayPreview) \
		meta_method(StopPreview) \
		meta_method(PlayEmitterPreview, "index") \
		meta_method(StopEmitterPreview, "index") \
		meta_method(RemoveEmitter, "index") \
		meta_method(AssignTextureToEmitter, "emitterIndex", "textureIndex") \
	}); \
	FieldEnd(EffectComponent, PropertyAndMethodInheritance) \
};
