#pragma once

#define ReflectTransCondition \
ReflectionField(TransCondition) \
{ \
	PropertyField \
	({ \
		meta_property(valueName) \
		meta_property(CompareParameter) \
		meta_property(cType) \
	}); \
	FieldEnd(TransCondition, PropertyOnly) \
};
