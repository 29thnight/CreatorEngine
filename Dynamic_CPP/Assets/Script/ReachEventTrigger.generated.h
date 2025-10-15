#pragma once

#define ReflectReachEventTrigger \
ReflectionScriptField(ReachEventTrigger) \
{ \
	PropertyField \
	({ \
		meta_property(m_triggerIndex) \
		meta_property(m_emitOnEnter) \
		meta_property(m_emitOnStay) \
		meta_property(m_emitOnExit) \
		meta_property(m_once) \
		meta_property(m_playerId) \
		meta_property(m_allPlayerPass) \
	}); \
	FieldEnd(ReachEventTrigger, PropertyOnly) \
};
