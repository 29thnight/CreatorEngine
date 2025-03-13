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
		if (m_animator.m_Skeleton) // Skeleton이 존재하는 경우
        {
            ShowBoneHierarchy(m_animator.m_Skeleton->m_rootBone);
        }
	});
}

void SceneObject::ShowBoneHierarchy(Bone* bone)
{
    if (!bone) return; // NULL 체크

    if (ImGui::TreeNode(bone->m_name.c_str())) // 현재 Bone을 TreeNode로 추가
    {
        for (Bone* child : bone->m_children) // 모든 자식 Bone을 탐색
        {
            ShowBoneHierarchy(child); // 재귀적으로 트리 구조를 그리기
        }
        ImGui::TreePop(); // TreeNode 닫기
    }
}
