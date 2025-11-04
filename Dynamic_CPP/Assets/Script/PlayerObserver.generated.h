#pragma once

#define ReflectPlayerObserver \
ReflectionScriptField(PlayerObserver) \
{ \
	PropertyField \
	({ \
		meta_property(screenOffset) \
		meta_property(WaitBeforeFade) \
	}); \
	FieldEnd(PlayerObserver, PropertyOnly) \
};
