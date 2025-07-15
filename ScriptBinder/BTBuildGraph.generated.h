#pragma once

#define ReflectBTBuildGraph \
ReflectionField(BTBuildGraph) \
{ \
	PropertyField \
	({ \
		meta_property(NodeList) \
	}); \
	FieldEnd(BTBuildGraph, PropertyOnly) \
};
