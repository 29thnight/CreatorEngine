#pragma once

#define ReflectTransCondition \
ReflectionField(TransCondition) \
{ \
	PropertyField \
	({ \
		meta_property(valueName) \
		meta_property(cType) \
		meta_property(CompareParameter) \
	}); \
	FieldEnd(TransCondition, PropertyOnly) \
};
