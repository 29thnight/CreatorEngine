#pragma once

#define ReflectStoryStaging \
ReflectionScriptField(StoryStaging) \
{ \
	PropertyField \
	({ \
		meta_property(stagingID) \
	}); \
	FieldEnd(StoryStaging, PropertyOnly) \
};
