#pragma once

#define ReflectPlayerObserver \
ReflectionScriptField(PlayerObserver) \
{ \
	PropertyField \
	({ \
		meta_property(screenOffset) \
		meta_property(WaitBeforeFade) \
		meta_property(m_isCinema) \
	}); \
	FieldEnd(PlayerObserver, PropertyOnly) \
};
