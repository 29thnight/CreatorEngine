#include "MeshRenderer.h"
#include "ReflectionImGuiHelper.h"
#include "DataSystem.h"
#include "Model.h"
#include "ShaderSystem.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ExternUI.h"
#include <d3d11shader.h>
#include <cstring>
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
            ImGui::GetContext("SelectMatarial").Open();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ELLIPSIS))
        {
                ImGui::OpenPopup("MaterialMenu");
        }
        if (ImGui::BeginPopup("MaterialMenu"))
        {
            if (ImGui::MenuItem("Instantiate") && meshRenderer->m_Material)
            {
                Material* newMat = Material::Instantiate(meshRenderer->m_Material);
                meshRenderer->m_Material = newMat;
                DataSystems->SaveMaterial(newMat);
            }
            ImGui::EndPopup();
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

			std::string shaderName = "No Shader";
			if (!meshRenderer->m_Material->m_shaderPSOName.empty())
			{
				shaderName = meshRenderer->m_Material->m_shaderPSOName;
			}
			ImGui::Text("Shader");
			ImGui::SameLine();
			ImGui::Button(shaderName.c_str(), ImVec2(200, 0));
			ImGui::SameLine();
            if (ImGui::Button(ICON_FA_BOX "##SelectShader"))
            {
				ShaderSystem->SetShaderSelectionTarget(meshRenderer->m_Material);
                ImGui::GetContext("SelectShader").Open();
            }
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_TRASH_CAN "##MatClearShader"))
			{
				meshRenderer->m_Material->ClearShaderPSO();
			}

            if (meshRenderer->m_Material->m_shaderPSO)
            {
                if (ImGui::TreeNode("Constants"))
                {
                    auto& pso = meshRenderer->m_Material->m_shaderPSO;
                    for (auto& [cbName, cb] : pso->GetConstantBuffers())
                    {
                        if (ImGui::TreeNode(cbName.c_str()))
                        {
                            auto& storage = meshRenderer->m_Material->m_cbufferValues[cbName];
                            if (storage.size() != cb.size) storage.resize(cb.size);
                            for (auto& var : cb.variables)
                            {
                                uint8_t* data = storage.data() + var.offset;
                                if (var.type == D3D_SVT_FLOAT)
                                {
                                    if (var.size == sizeof(float))
                                    {
                                        float val; std::memcpy(&val, data, sizeof(float));
                                        if (ImGui::DragFloat(var.name.c_str(), &val))
                                        {
                                            std::memcpy(data, &val, sizeof(float));
                                            pso->UpdateVariable(cbName, var.name, &val, sizeof(float));
                                        }
                                    }
                                    else if (var.size == sizeof(float) * 4)
                                    {
                                        float vals[4]; std::memcpy(vals, data, sizeof(vals));
                                        if (var.name.find("Color") != std::string::npos)
                                        {
                                            if (ImGui::ColorEdit4(var.name.c_str(), vals))
                                            {
                                                std::memcpy(data, vals, sizeof(vals));
                                                pso->UpdateVariable(cbName, var.name, vals, sizeof(vals));
                                            }
                                        }
                                        else if (ImGui::DragFloat4(var.name.c_str(), vals))
                                        {
                                            std::memcpy(data, vals, sizeof(vals));
                                            pso->UpdateVariable(cbName, var.name, vals, sizeof(vals));
                                        }
                                    }
                                }
                                else if (var.type == D3D_SVT_INT)
                                {
                                    if (var.size == sizeof(int))
                                    {
                                        int val; std::memcpy(&val, data, sizeof(int));
                                        if (ImGui::DragInt(var.name.c_str(), &val))
                                        {
                                            std::memcpy(data, &val, sizeof(int));
                                            pso->UpdateVariable(cbName, var.name, &val, sizeof(int));
                                        }
                                    }
                                }
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }
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
		// static 변수는 ImGui 컨텍스트 내에서 상태를 유지하기 위해 사용됩니다.
		// 여러 MeshRenderer를 동시에 편집하는 경우, 이 static 변수들을
		// 각 MeshRenderer 인스턴스에 연결된 상태로 관리해야 할 수 있습니다.
		static Mesh* selectedMesh = nullptr;
		static std::vector<float> LODThresholdsSetting; // 0.0f (Culled)는 포함하지 않음

		// 현재 선택된 Mesh가 변경되었는지 확인하고 LODThresholdsSetting을 업데이트
		if (selectedMesh != meshRenderer->m_Mesh)
		{
			selectedMesh = meshRenderer->m_Mesh;
			if (selectedMesh) // Mesh가 할당된 경우에만 LODThresholds를 가져옴
			{
				LODThresholdsSetting.clear();
				// GetLODThresholds는 0.0f (Culled)를 제외한 실제 LOD 임계값만 반환한다고 가정
				LODThresholdsSetting = selectedMesh->GetLODThresholds();
			}
			else
			{
				LODThresholdsSetting.clear(); // Mesh가 없으면 임계값도 비움
			}
		}

		if (!selectedMesh) // selectedMesh가 여전히 null인 경우 (meshRenderer->m_Mesh가 null)
		{
			ImGui::Text("No Mesh assigned.");
		}
		else
		{
			ImGui::Text("Mesh: %s", selectedMesh->GetName().c_str());
			ImGui::Checkbox("Enable LODGroup", &meshRenderer->m_isEnableLOD);

			if (meshRenderer->m_isEnableLOD)
			{
				// --- LOD 임계값 UI ---
				float barHeight = 30.0f;
				float barPadding = 20.0f; // 막대 좌우 패딩
				float barWidth = ImGui::GetContentRegionAvail().x - barPadding * 2;
				if (barWidth < 100.0f) barWidth = 100.0f; // 최소 너비

				ImVec2 p = ImGui::GetCursorScreenPos(); // 그리기 영역의 좌상단 위치
				ImVec2 barMin = ImVec2(p.x + barPadding, p.y);
				ImVec2 barMax = ImVec2(barMin.x + barWidth, barMin.y + barHeight);

				ImDrawList* draw_list = ImGui::GetWindowDrawList();

				// 배경 막대 그리기
				draw_list->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 50, 255)); // 어두운 회색 배경
				draw_list->AddRect(barMin, barMax, IM_COL32(100, 100, 100, 255)); // 테두리

				// LOD별 색상 (예시)
				ImU32 lodColors[] = {
					IM_COL32(0, 200, 0, 255),   // LOD0 (녹색)
					IM_COL32(200, 200, 0, 255), // LOD1 (노란색)
					IM_COL32(200, 0, 0, 255),   // LOD2 (빨간색)
					IM_COL32(100, 100, 100, 255) // Culled (회색) - 마지막 세그먼트용
				};
				const int numLODColors = sizeof(lodColors) / sizeof(lodColors[0]);

				// LOD 세그먼트 및 텍스트 그리기
				// 모든 경계값을 포함하는 벡터 (1.0f, LOD0_threshold, ..., LOD(N-1)_threshold, 0.0f)
				std::vector<float> all_boundaries;
				all_boundaries.push_back(1.0f); // LOD0의 시작 (가장 높은 디테일)
				for (float t : LODThresholdsSetting) {
					all_boundaries.push_back(t);
				}
				all_boundaries.push_back(0.0f); // Culled의 끝 (가장 낮은 디테일)

				float currentSegmentStartX = barMin.x;
				for (int i = 0; i < all_boundaries.size() - 1; ++i)
				{
					float leftBoundary = all_boundaries[i];
					float rightBoundary = all_boundaries[i + 1];

					// 현재 및 다음 임계값의 차이를 기반으로 세그먼트 너비 계산
					// (leftBoundary는 항상 rightBoundary보다 크거나 같음)
					float segmentPixelWidth = (leftBoundary - rightBoundary) * barWidth;

					ImVec2 segmentMin = ImVec2(currentSegmentStartX, barMin.y);
					ImVec2 segmentMax = ImVec2(currentSegmentStartX + segmentPixelWidth, barMax.y);

					// LOD 세그먼트 배경 그리기
					draw_list->AddRectFilled(segmentMin, segmentMax, lodColors[std::min(i, numLODColors - 1)]);
					draw_list->AddRect(segmentMin, segmentMax, IM_COL32(255, 255, 255, 100)); // 테두리

					// LOD 텍스트 그리기
					std::string lodText;
					if (i < LODThresholdsSetting.size()) {
						lodText = "LOD" + std::to_string(i);
					}
					else {
						lodText = "Culled"; // 마지막 세그먼트
					}
					ImVec2 textPos = ImVec2(segmentMin.x + 5, segmentMin.y + (barHeight - ImGui::GetTextLineHeight()) / 2);
					draw_list->AddText(textPos, IM_COL32(255, 255, 255, 255), lodText.c_str());

					currentSegmentStartX += segmentPixelWidth;
				}

				// 드래그 가능한 임계값 핸들 그리기 및 처리
				// 핸들은 LODThresholdsSetting의 각 요소에 해당하며, 이는 각 LOD의 오른쪽 경계입니다.
				for (int i = 0; i < LODThresholdsSetting.size(); ++i) // 0.0f (Culled)는 핸들이 없음
				{
					float handleWidth = 10.0f; // 드래그 가능한 핸들의 너비
					// 핸들 위치 계산: 1.0f가 바의 왼쪽 끝, 0.0f가 바의 오른쪽 끝
					float handleX = barMin.x + (1.0f - LODThresholdsSetting[i]) * barWidth;

					ImVec2 handleMin = ImVec2(handleX - handleWidth / 2, barMin.y);
					ImVec2 handleMax = ImVec2(handleX + handleWidth / 2, barMax.y);

					// 드래그를 위한 투명 버튼 생성
					ImGui::SetCursorScreenPos(handleMin);
					ImGui::InvisibleButton(("LOD_Handle_" + std::to_string(i)).c_str(), ImVec2(handleWidth, barHeight));

					bool isHovered = ImGui::IsItemHovered();
					bool isActive = ImGui::IsItemActive();

					// 핸들 그리기
					draw_list->AddRectFilled(handleMin, handleMax, isActive ? IM_COL32(255, 255, 0, 255) : (isHovered ? IM_COL32(200, 200, 0, 255) : IM_COL32(150, 150, 0, 255)));
					draw_list->AddRect(handleMin, handleMax, IM_COL32(255, 255, 255, 255)); // 테두리

					// 드래그 처리
					if (isActive && ImGui::IsMouseDragging(0))
					{
						float mouseDeltaX = ImGui::GetIO().MouseDelta.x;
						float deltaThreshold = mouseDeltaX / barWidth; // 0-1 범위의 델타

						// 유니티처럼 오른쪽으로 드래그하면 임계값이 감소하도록 변경
						float newThreshold = LODThresholdsSetting[i] - deltaThreshold;

						// 0.0f ~ 1.0f 범위로 클램프
						newThreshold = std::max(0.0f, std::min(1.0f, newThreshold));

						// 인접한 임계값과의 겹침 방지
						// newThreshold는 왼쪽 임계값(더 높은 값)보다 작아야 함
						if (i > 0) {
							newThreshold = std::min(newThreshold, LODThresholdsSetting[i - 1] - 0.001f);
						}
						// newThreshold는 오른쪽 임계값(더 낮은 값)보다 커야 함
						if (i < LODThresholdsSetting.size() - 1) {
							newThreshold = std::max(newThreshold, LODThresholdsSetting[i + 1] + 0.001f);
						}
						else { // 마지막 LOD 임계값은 0.0f보다 커야 함
							newThreshold = std::max(newThreshold, 0.0f + 0.001f);
						}

						LODThresholdsSetting[i] = newThreshold;
					}
				}

				// 그리기 영역을 지나 커서 위치 이동
				ImGui::Dummy(ImVec2(barWidth + barPadding * 2, barHeight));

				// 디버깅/정보를 위한 현재 임계값 표시
				ImGui::Text("LOD Thresholds:");
				for (int i = 0; i < LODThresholdsSetting.size(); ++i) {
					ImGui::SameLine();
					ImGui::Text("LOD%d: %.3f", i, LODThresholdsSetting[i]);
				}

				// LOD 추가/제거 버튼 (선택 사항, 유니티와 유사한 동작을 위해 유용)
				if (ImGui::Button("Add LOD")) {
					// 새로운 LOD를 추가합니다.
					float newThreshold;
					if (LODThresholdsSetting.empty()) {
						newThreshold = 0.5f; // 첫 LOD는 기본값 0.5f
					}
					else {
						// 마지막 LOD와 Culled (0.0f) 사이의 중간값
						newThreshold = (LODThresholdsSetting.back() + 0.0f) / 2.0f;
					}
					LODThresholdsSetting.push_back(newThreshold);
					// 내림차순 정렬 유지
					std::sort(LODThresholdsSetting.rbegin(), LODThresholdsSetting.rend());
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove Last LOD")) {
					if (!LODThresholdsSetting.empty()) { // Culled는 제거하지 않으므로, 실제 LOD가 하나라도 있어야 제거 가능
						LODThresholdsSetting.pop_back();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Apply LOD Thresholds"))
				{
					// LODThresholdsSetting을 selectedMesh->GenerateLODs에 직접 적용
					selectedMesh->GenerateLODs(LODThresholdsSetting);
					// Debug->Log는 예시이므로 실제 로깅 시스템에 맞게 변경하세요.
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