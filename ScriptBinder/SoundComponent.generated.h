#pragma once

#define ReflectSoundComponent \
ReflectionFieldInheritance(SoundComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(clipKey) \
		meta_property(bus) \
		meta_property(volume) \
		meta_property(pitch) \
		meta_property(priority) \
		meta_property(spatialBlend) \
		meta_property(minDistance) \
		meta_property(maxDistance) \
		meta_property(reverbLevel) \
		meta_property(reverbIndex) \
		meta_property(rolloff) \
		meta_property(velocity) \
		meta_property(localRolloffCurve) \
		meta_property(loop) \
		meta_property(playOnStart) \
		meta_property(spatial) \
		meta_property(useReverbSend) \
	}); \
	MethodField \
	({ \
		meta_method(Play) \
		meta_method(Stop) \
		meta_method(Pause, "pause") \
		meta_method(IsPlaying) \
		meta_method(PlayOneShot) \
	}); \
	FieldEnd(SoundComponent, PropertyAndMethodInheritance) \
};
