#include "SceneObject.h"

SceneObject::SceneObject(const std::string_view& name, SceneObject::Index index, SceneObject::Index parentIndex) :
	m_name(name),
	m_index(index),
	m_parentIndex(parentIndex)
{
}
