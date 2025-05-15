#pragma once

#define ReflectSkeleton \
ReflectionField(Skeleton) \
{ \
	PropertyField \
	({ \
		meta_property(m_animations) \
		meta_property(m_rootTransform) \
	}); \
	FieldEnd(Skeleton, PropertyOnly) \
};
