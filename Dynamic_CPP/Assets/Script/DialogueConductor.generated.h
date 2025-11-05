#pragma once

#define ReflectDialogueConductor \
ReflectionScriptField(DialogueConductor) \
{ \
	PropertyField \
	({ \
		meta_property(autoPlayDelay) \
		meta_property(waitAtLast) \
		meta_property(hideWhenReset) \
		meta_property(autoExitAfterFinish) \
		meta_property(autoExitDelay) \
	}); \
	FieldEnd(DialogueConductor, PropertyOnly) \
};
