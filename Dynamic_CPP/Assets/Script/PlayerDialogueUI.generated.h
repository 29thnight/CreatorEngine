#pragma once

#define ReflectPlayerDialogueUI \
ReflectionScriptField(PlayerDialogueUI) \
{ \
	PropertyField \
	({ \
		meta_property(screenOffset) \
		meta_property(sideOffsetPixels) \
	}); \
	FieldEnd(PlayerDialogueUI, PropertyOnly) \
};
