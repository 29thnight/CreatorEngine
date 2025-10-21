#pragma once

#define ReflectGameObject \
ReflectionFieldInheritance(GameObject, Object) \
{ \
	PropertyField \
	({ \
		meta_property(m_attachedSoketID) \
		meta_property(m_transform) \
		meta_property(m_index) \
		meta_property(m_parentIndex) \
		meta_property(m_rootIndex) \
		meta_property(m_collisionType) \
		meta_property(m_prefabFileGuid) \
		meta_property(m_childrenIndices) \
		meta_property(m_tag) \
		meta_property(m_layer) \
		meta_property(m_components) \
		meta_property(m_gameObjectType) \
		meta_property(m_isStatic) \
	}); \
	FieldEnd(GameObject, PropertyOnlyInheritance) \
};
