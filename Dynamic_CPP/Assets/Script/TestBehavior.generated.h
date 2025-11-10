#pragma once

#define ReflectTestBehavior \
ReflectionScriptField(TestBehavior) \
{ \
	PropertyField \
	({ \
		meta_property(testValue) \
		meta_property(testString) \
		meta_property(m_chargingTime) \
		meta_property(moveDir) \
	}); \
	FieldEnd(TestBehavior, PropertyOnly) \
};
