#pragma once

#define ReflectConditionParameter \
ReflectionField(ConditionParameter) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
		meta_property(fValue) \
		meta_property(iValue) \
		meta_property(vType) \
		meta_property(bValue) \
		meta_property(tValue) \
	}); \
	FieldEnd(ConditionParameter, PropertyOnly) \
};
