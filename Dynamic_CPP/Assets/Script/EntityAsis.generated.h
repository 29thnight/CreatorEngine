#pragma once

#define ReflectEntityAsis \
ReflectionScriptField(EntityAsis) \
{ \
	PropertyField \
	({ \
		meta_property(maxHP) \
		meta_property(moveSpeed) \
		meta_property(graceperiod) \
		meta_property(staggerDuration) \
		meta_property(resurrectionMultiple) \
		meta_property(resurrectionTime) \
		meta_property(resurrectionHP) \
		meta_property(resurrectionGracePeriod) \
		meta_property(tailPurificationDuration) \
		meta_property(mouthPurificationDuration) \
		meta_property(purificationRange) \
		meta_property(maxTailCapacity) \
		meta_property(maxPollutionGauge) \
		meta_property(pollutionCoreAmount) \
	}); \
	FieldEnd(EntityAsis, PropertyOnly) \
};
