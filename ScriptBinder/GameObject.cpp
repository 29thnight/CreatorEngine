#include "GameObject.h"

GameObject::GameObject(const std::string_view& name, GameObject::Index index, GameObject::Index parentIndex) : 
    m_name(name.data()), 
    m_index(index), 
    m_parentIndex(parentIndex)
{
}

std::string GameObject::ToString() const
{
    return m_name.ToString();
}

void GameObject::ShowBoneHierarchy(Bone* bone)
{
}

void GameObject::RenderBoneEditor()
{
}

void GameObject::EditorMeshRenderer()
{
}
