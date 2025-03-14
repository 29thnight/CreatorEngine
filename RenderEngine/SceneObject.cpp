#include "SceneObject.h"
#include "Skeleton.h"
#include "ImGuiRegister.h"
#include "Material.h"

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

	RenderBoneEditor();
}

void SceneObject::ShowBoneHierarchy(Bone* bone)
{
    if (!bone) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (bone == selectedBone)
        flags |= ImGuiTreeNodeFlags_Selected;

    if (ImGui::TreeNodeEx(bone->m_name.c_str(), flags))
    {
        if (ImGui::IsItemClicked()) // Bone 클릭 시 선택
        {
            selectedBone = bone;
        }

        for (Bone* child : bone->m_children)
        {
            ShowBoneHierarchy(child);
        }

        ImGui::TreePop();
    }
}

void SceneObject::RenderBoneEditor()
{
	ImGui::ContextRegister("Bone Transform", [&]()
	{
		if (selectedBone)
		{
			ImGui::Text(selectedBone->m_name.c_str());
			ImGui::Separator();
			ImGui::Text("Position");
			ImGui::DragFloat3("##Position", &selectedBone->m_globalTransform.r[3].m128_f32[0], -1000, 1000);
			ImGui::Text("Rotation");
			ImGui::DragFloat3("##Rotation", &selectedBone->m_globalTransform.r[0].m128_f32[0], -3.14f, 3.14f);
			ImGui::Text("Scale");
			ImGui::DragFloat3("##Scale", &selectedBone->m_globalTransform.r[0].m128_f32[1], 0.1f, 10);
			ImGui::Text("Bone Index");
			ImGui::InputInt("##Bone Index", &selectedBone->m_index);
		}
	});
}

void SceneObject::EditorMeshRenderer()
{
	ImGui::ContextRegister("Mesh Renderer", [&]()
	{
		ImGui::Checkbox("Is Enabled", &m_meshRenderer.m_IsEnabled);
		ImGui::Text("Material");
		ImGui::ColorEdit3("Base Color", &m_meshRenderer.m_Material->m_materialInfo.m_baseColor.x);
		ImGui::SliderFloat("Metallic", &m_meshRenderer.m_Material->m_materialInfo.m_metallic, 0, 1);
		ImGui::SliderFloat("Roughness", &m_meshRenderer.m_Material->m_materialInfo.m_roughness, 0, 1);
		//ImGui::SliderFloat("Emissive", &m_meshRenderer.m_Material->m_materialInfo.emissive, 0, 1);
	});
}

