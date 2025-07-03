#pragma once

#define ReflectAssetEntry \
ReflectionField(AssetEntry) \
{ \
	PropertyField \
	({ \
		meta_property(assetTypeID) \
		meta_property(assetName) \
	}); \
	FieldEnd(AssetEntry, PropertyOnly) \
};
