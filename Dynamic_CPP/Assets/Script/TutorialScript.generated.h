#pragma once

#define ReflectTutorialScript \
ReflectionScriptField(TutorialScript) \
{ \
	PropertyField \
	({ \
		meta_property(EventID) \
	}); \
	FieldEnd(TutorialScript, PropertyOnly) \
};
