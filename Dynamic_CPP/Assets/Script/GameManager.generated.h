#pragma once

#define ReflectGameManager \
ReflectionScriptField(GameManager) \
{ \
	PropertyField \
	({ \
		meta_property(m_isTestReward) \
	}); \
	MethodField \
	({ \
		meta_method(LoadTestScene) \
		meta_method(SwitchTestScene) \
	}); \
	FieldEnd(GameManager, PropertyAndMethod) \
};
