#pragma once

#define ReflectAssetBundle \
ReflectionField(AssetBundle) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
		meta_property(path) \
		meta_property(assets) \
	}); \
	FieldEnd(AssetBundle, PropertyOnly) \
};
