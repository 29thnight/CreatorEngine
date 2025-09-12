#pragma once

#define ReflectGameManager \
ReflectionScriptField(GameManager) \
{ \
	MethodField \
	({ \
		meta_method(LoadTestScene) \
		meta_method(SwitchTestScene) \
	}); \
	FieldEnd(GameManager, MethodOnly) \
};
