#include "SceneObject.h"
#include "Skeleton.h"
#include "ImGuiRegister.h"

SceneObject::SceneObject(const std::string_view& name, SceneObject::Index index, SceneObject::Index parentIndex) :
	m_name(name),
	m_index(index),
	m_parentIndex(parentIndex)
{
	ImGui::ContextRegister("Bone Hierarchy", [&]()
	{
		if (m_animator.m_Skeleton) // Skeleton�� �����ϴ� ���
        {
            ShowBoneHierarchy(m_animator.m_Skeleton->m_rootBone);
        }
	});
}

void SceneObject::ShowBoneHierarchy(Bone* bone)
{
    if (!bone) return; // NULL üũ

    if (ImGui::TreeNode(bone->m_name.c_str())) // ���� Bone�� TreeNode�� �߰�
    {
        for (Bone* child : bone->m_children) // ��� �ڽ� Bone�� Ž��
        {
            ShowBoneHierarchy(child); // ��������� Ʈ�� ������ �׸���
        }
        ImGui::TreePop(); // TreeNode �ݱ�
    }
}
