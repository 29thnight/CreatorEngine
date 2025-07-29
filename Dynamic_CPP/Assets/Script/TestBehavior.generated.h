#pragma once

#define ReflectTestBehavior \
ReflectionScriptField(TestBehavior) \
{ \
	PropertyField \
	({ \
		meta_property(testValue) \
		meta_property(testString) \
	}); \
	FieldEnd(TestBehavior, PropertyOnly) \
};
