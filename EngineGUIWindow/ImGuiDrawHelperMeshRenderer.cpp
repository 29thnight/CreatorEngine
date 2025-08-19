#include "MeshRenderer.h"
#include "ReflectionImGuiHelper.h"
#include "DataSystem.h"
#include "Model.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ExternUI.h"
#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */

void ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer)
{
	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Element ");
		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

		if (meshRenderer->m_Material && !meshRenderer->m_Material->m_name.empty())
		{
			ImGui::Button(meshRenderer->m_Material->m_name.c_str(), ImVec2(250, 0));
		}
		else
		{
			ImGui::Button("No Material", ImVec2(250, 0));
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_BOX))
		{
			ImGui::GetContext("SelectMaterial").Open();
		}
		ImGui::PopStyleVar(2);
	}

	if (ImGui::CollapsingHeader("MaterialInfo", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const auto& mat_type = Meta::Find("Material");
		const auto& mat_info_type = Meta::Find("MaterialInfomation");
		if (nullptr != meshRenderer->m_Material)
		{
			auto& mat_info = meshRenderer->m_Material->m_materialInfo;
			ImGui::ColorEdit4("base color", &mat_info.m_baseColor.x);

			ImGui::SliderFloat("metalic", &mat_info.m_metallic, 0.f, 1.f);

			ImGui::SliderFloat("roughness", &mat_info.m_roughness, 0.f, 1.f);

			ImGui::DragScalar("bitflag", ImGuiDataType_S32, &mat_info.m_bitflag);
		}
		else
		{
			ImGui::Text("No Material assigned.");
		}
		const auto& mat_render_type = Meta::FindEnum("MaterialRenderingMode");
		for (auto& enumProp : mat_type->properties)
		{
			if (enumProp.typeID == TypeTrait::GUIDCreator::GetTypeID<MaterialRenderingMode>())
			{
				if (nullptr != meshRenderer->m_Material)
				{
					Meta::DrawEnumProperty((int*)&meshRenderer->m_Material->m_renderingMode, mat_render_type, enumProp);
				}
				else
				{
					ImGui::Text("No Material assigned.");
				}
				break;
			}
		}
	}

	if (ImGui::CollapsingHeader("LightMapping", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const auto& lightmap_type = Meta::Find("LightMapping");
		Meta::DrawProperties(&meshRenderer->m_LightMapping, *lightmap_type);
	}

	if (ImGui::CollapsingHeader("LODGroupShared", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// static ������ ImGui ���ؽ�Ʈ ������ ���¸� �����ϱ� ���� ���˴ϴ�.
		// ���� MeshRenderer�� ���ÿ� �����ϴ� ���, �� static ��������
		// �� MeshRenderer �ν��Ͻ��� ����� ���·� �����ؾ� �� �� �ֽ��ϴ�.
		static Mesh* selectedMesh = nullptr;
		static std::vector<float> LODThresholdsSetting; // 0.0f (Culled)�� �������� ����

		// ���� ���õ� Mesh�� ����Ǿ����� Ȯ���ϰ� LODThresholdsSetting�� ������Ʈ
		if (selectedMesh != meshRenderer->m_Mesh)
		{
			selectedMesh = meshRenderer->m_Mesh;
			if (selectedMesh) // Mesh�� �Ҵ�� ��쿡�� LODThresholds�� ������
			{
				LODThresholdsSetting.clear();
				// GetLODThresholds�� 0.0f (Culled)�� ������ ���� LOD �Ӱ谪�� ��ȯ�Ѵٰ� ����
				LODThresholdsSetting = selectedMesh->GetLODThresholds();
			}
			else
			{
				LODThresholdsSetting.clear(); // Mesh�� ������ �Ӱ谪�� ���
			}
		}

		if (!selectedMesh) // selectedMesh�� ������ null�� ��� (meshRenderer->m_Mesh�� null)
		{
			ImGui::Text("No Mesh assigned.");
		}
		else
		{
			ImGui::Text("Mesh: %s", selectedMesh->GetName().c_str());
			ImGui::Checkbox("Enable LODGroup", &meshRenderer->m_isEnableLOD);

			if (meshRenderer->m_isEnableLOD)
			{
				// --- LOD �Ӱ谪 UI ---
				float barHeight = 30.0f;
				float barPadding = 20.0f; // ���� �¿� �е�
				float barWidth = ImGui::GetContentRegionAvail().x - barPadding * 2;
				if (barWidth < 100.0f) barWidth = 100.0f; // �ּ� �ʺ�

				ImVec2 p = ImGui::GetCursorScreenPos(); // �׸��� ������ �»�� ��ġ
				ImVec2 barMin = ImVec2(p.x + barPadding, p.y);
				ImVec2 barMax = ImVec2(barMin.x + barWidth, barMin.y + barHeight);

				ImDrawList* draw_list = ImGui::GetWindowDrawList();

				// ��� ���� �׸���
				draw_list->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 50, 255)); // ��ο� ȸ�� ���
				draw_list->AddRect(barMin, barMax, IM_COL32(100, 100, 100, 255)); // �׵θ�

				// LOD�� ���� (����)
				ImU32 lodColors[] = {
					IM_COL32(0, 200, 0, 255),   // LOD0 (���)
					IM_COL32(200, 200, 0, 255), // LOD1 (�����)
					IM_COL32(200, 0, 0, 255),   // LOD2 (������)
					IM_COL32(100, 100, 100, 255) // Culled (ȸ��) - ������ ���׸�Ʈ��
				};
				const int numLODColors = sizeof(lodColors) / sizeof(lodColors[0]);

				// LOD ���׸�Ʈ �� �ؽ�Ʈ �׸���
				// ��� ��谪�� �����ϴ� ���� (1.0f, LOD0_threshold, ..., LOD(N-1)_threshold, 0.0f)
				std::vector<float> all_boundaries;
				all_boundaries.push_back(1.0f); // LOD0�� ���� (���� ���� ������)
				for (float t : LODThresholdsSetting) {
					all_boundaries.push_back(t);
				}
				all_boundaries.push_back(0.0f); // Culled�� �� (���� ���� ������)

				float currentSegmentStartX = barMin.x;
				for (int i = 0; i < all_boundaries.size() - 1; ++i)
				{
					float leftBoundary = all_boundaries[i];
					float rightBoundary = all_boundaries[i + 1];

					// ���� �� ���� �Ӱ谪�� ���̸� ������� ���׸�Ʈ �ʺ� ���
					// (leftBoundary�� �׻� rightBoundary���� ũ�ų� ����)
					float segmentPixelWidth = (leftBoundary - rightBoundary) * barWidth;

					ImVec2 segmentMin = ImVec2(currentSegmentStartX, barMin.y);
					ImVec2 segmentMax = ImVec2(currentSegmentStartX + segmentPixelWidth, barMax.y);

					// LOD ���׸�Ʈ ��� �׸���
					draw_list->AddRectFilled(segmentMin, segmentMax, lodColors[std::min(i, numLODColors - 1)]);
					draw_list->AddRect(segmentMin, segmentMax, IM_COL32(255, 255, 255, 100)); // �׵θ�

					// LOD �ؽ�Ʈ �׸���
					std::string lodText;
					if (i < LODThresholdsSetting.size()) {
						lodText = "LOD" + std::to_string(i);
					}
					else {
						lodText = "Culled"; // ������ ���׸�Ʈ
					}
					ImVec2 textPos = ImVec2(segmentMin.x + 5, segmentMin.y + (barHeight - ImGui::GetTextLineHeight()) / 2);
					draw_list->AddText(textPos, IM_COL32(255, 255, 255, 255), lodText.c_str());

					currentSegmentStartX += segmentPixelWidth;
				}

				// �巡�� ������ �Ӱ谪 �ڵ� �׸��� �� ó��
				// �ڵ��� LODThresholdsSetting�� �� ��ҿ� �ش��ϸ�, �̴� �� LOD�� ������ ����Դϴ�.
				for (int i = 0; i < LODThresholdsSetting.size(); ++i) // 0.0f (Culled)�� �ڵ��� ����
				{
					float handleWidth = 10.0f; // �巡�� ������ �ڵ��� �ʺ�
					// �ڵ� ��ġ ���: 1.0f�� ���� ���� ��, 0.0f�� ���� ������ ��
					float handleX = barMin.x + (1.0f - LODThresholdsSetting[i]) * barWidth;

					ImVec2 handleMin = ImVec2(handleX - handleWidth / 2, barMin.y);
					ImVec2 handleMax = ImVec2(handleX + handleWidth / 2, barMax.y);

					// �巡�׸� ���� ���� ��ư ����
					ImGui::SetCursorScreenPos(handleMin);
					ImGui::InvisibleButton(("LOD_Handle_" + std::to_string(i)).c_str(), ImVec2(handleWidth, barHeight));

					bool isHovered = ImGui::IsItemHovered();
					bool isActive = ImGui::IsItemActive();

					// �ڵ� �׸���
					draw_list->AddRectFilled(handleMin, handleMax, isActive ? IM_COL32(255, 255, 0, 255) : (isHovered ? IM_COL32(200, 200, 0, 255) : IM_COL32(150, 150, 0, 255)));
					draw_list->AddRect(handleMin, handleMax, IM_COL32(255, 255, 255, 255)); // �׵θ�

					// �巡�� ó��
					if (isActive && ImGui::IsMouseDragging(0))
					{
						float mouseDeltaX = ImGui::GetIO().MouseDelta.x;
						float deltaThreshold = mouseDeltaX / barWidth; // 0-1 ������ ��Ÿ

						// ����Ƽó�� ���������� �巡���ϸ� �Ӱ谪�� �����ϵ��� ����
						float newThreshold = LODThresholdsSetting[i] - deltaThreshold;

						// 0.0f ~ 1.0f ������ Ŭ����
						newThreshold = std::max(0.0f, std::min(1.0f, newThreshold));

						// ������ �Ӱ谪���� ��ħ ����
						// newThreshold�� ���� �Ӱ谪(�� ���� ��)���� �۾ƾ� ��
						if (i > 0) {
							newThreshold = std::min(newThreshold, LODThresholdsSetting[i - 1] - 0.001f);
						}
						// newThreshold�� ������ �Ӱ谪(�� ���� ��)���� Ŀ�� ��
						if (i < LODThresholdsSetting.size() - 1) {
							newThreshold = std::max(newThreshold, LODThresholdsSetting[i + 1] + 0.001f);
						}
						else { // ������ LOD �Ӱ谪�� 0.0f���� Ŀ�� ��
							newThreshold = std::max(newThreshold, 0.0f + 0.001f);
						}

						LODThresholdsSetting[i] = newThreshold;
					}
				}

				// �׸��� ������ ���� Ŀ�� ��ġ �̵�
				ImGui::Dummy(ImVec2(barWidth + barPadding * 2, barHeight));

				// �����/������ ���� ���� �Ӱ谪 ǥ��
				ImGui::Text("LOD Thresholds:");
				for (int i = 0; i < LODThresholdsSetting.size(); ++i) {
					ImGui::SameLine();
					ImGui::Text("LOD%d: %.3f", i, LODThresholdsSetting[i]);
				}

				// LOD �߰�/���� ��ư (���� ����, ����Ƽ�� ������ ������ ���� ����)
				if (ImGui::Button("Add LOD")) {
					// ���ο� LOD�� �߰��մϴ�.
					float newThreshold;
					if (LODThresholdsSetting.empty()) {
						newThreshold = 0.5f; // ù LOD�� �⺻�� 0.5f
					}
					else {
						// ������ LOD�� Culled (0.0f) ������ �߰���
						newThreshold = (LODThresholdsSetting.back() + 0.0f) / 2.0f;
					}
					LODThresholdsSetting.push_back(newThreshold);
					// �������� ���� ����
					std::sort(LODThresholdsSetting.rbegin(), LODThresholdsSetting.rend());
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove Last LOD")) {
					if (!LODThresholdsSetting.empty()) { // Culled�� �������� �����Ƿ�, ���� LOD�� �ϳ��� �־�� ���� ����
						LODThresholdsSetting.pop_back();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Apply LOD Thresholds"))
				{
					// LODThresholdsSetting�� selectedMesh->GenerateLODs�� ���� ����
					selectedMesh->GenerateLODs(LODThresholdsSetting);
					// Debug->Log�� �����̹Ƿ� ���� �α� �ý��ۿ� �°� �����ϼ���.
					// Debug->Log("Applied LOD Thresholds to Mesh: " + selectedMesh->GetName());
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("ShadowSetting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable Shadow Receive", &meshRenderer->m_shadowRecive);
		ImGui::Checkbox("Enable Shadow Cast", &meshRenderer->m_shadowCast);
	}

	if (DataSystems->m_trasfarMaterial)
	{
		std::string name{};
		if (meshRenderer->m_Material)
		{
			name = meshRenderer->m_Material->m_name;
		}
		Meta::MakeCustomChangeCommand(
			[=]
			{
				if (!name.empty() && DataSystems->Materials.find(name) != DataSystems->Materials.end())
				{
					meshRenderer->m_Material = DataSystems->Materials[name].get();
				}
				else
				{
					meshRenderer->m_Material = nullptr;
				}
			},
			[=]
			{
				meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
			});

		meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
		DataSystems->m_trasfarMaterial = nullptr;
	}
}