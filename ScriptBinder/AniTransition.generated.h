#pragma once

#define ReflectAniTransition \
ReflectionField(AniTransition) \
{ \
	PropertyField \
	({ \
		meta_property(conditions) \
		meta_property(curState) \
		meta_property(nextState) \
		meta_property(tranTime) \
		meta_property(exitTime) \
	}); \
	FieldEnd(AniTransition, PropertyOnly) \
};
