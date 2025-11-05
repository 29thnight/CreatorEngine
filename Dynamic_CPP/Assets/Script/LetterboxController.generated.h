#pragma once

#define ReflectLetterboxController \
ReflectionScriptField(LetterboxController) \
{ \
	PropertyField \
	({ \
		meta_property(barHeight) \
		meta_property(animDuration) \
		meta_property(startInCinema) \
		meta_property(topBarName) \
		meta_property(bottomBarName) \
	}); \
	MethodField \
	({ \
		meta_method(EnterCinemaMode) \
		meta_method(ExitCinemaMode) \
		meta_method(TestCinemaMode) \
	}); \
	FieldEnd(LetterboxController, PropertyAndMethod) \
};
