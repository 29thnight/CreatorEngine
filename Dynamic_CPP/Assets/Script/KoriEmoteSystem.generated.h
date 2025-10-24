#pragma once

#define ReflectKoriEmoteSystem \
ReflectionScriptField(KoriEmoteSystem) \
{ \
	PropertyField \
	({ \
		meta_property(m_emoteOffset) \
		meta_property(m_emoteChangeInterval) \
	}); \
	MethodField \
	({ \
		meta_method(PlaySmile) \
		meta_method(PlaySick) \
		meta_method(PlayStunned) \
		meta_method(PlayHappy) \
	}); \
	FieldEnd(KoriEmoteSystem, PropertyAndMethod) \
};
