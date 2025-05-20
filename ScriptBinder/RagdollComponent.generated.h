#pragma once

#define ReflectRagdollComponent \
ReflectionFieldInheritance(RagdollComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_bIsRagdoll) \
	}); \
	FieldEnd(RagdollComponent, PropertyOnlyInheritance) \
};
