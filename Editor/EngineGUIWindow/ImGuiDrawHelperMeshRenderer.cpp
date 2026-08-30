#include "MeshRenderer.h"
#include "MaterialScriptBinding.h"
#include "MaterialPropertyPacker.h"
#include "ShaderMeta.h"
#include "StandardMaterialProperty.h"
#include "ReflectionTypedDraw.h"
#include "EditorImGuiTexture.h"
#include "ReflectionImGuiHelper.h"
#include "DataSystem.h"
#include "EditorAssetDatabase.h"
#include "EditorAssetPresentation.h"
#include "Model.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ExternUI.h"
#include <d3d11shader.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <span>
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
            EditorAssetPresentation::Get().OpenMaterialPicker();
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
                // I5-M5 S4 — runtime 인스턴스는 비영속이다. asset cache에
                // 등록하지 않고 독립 .asset으로 저장하지 않는다(InstantiateShared
                // 계약 비승계). 새 저작 자산이 필요하면 그것은 자산 복제
                // (DuplicateMaterialAsset)의 몫이다.
                meshRenderer->m_Material = MaterialScriptBinding::InstantiateOwned(
                    *meshRenderer->m_Material, {});
                // S2c-2a — 인스턴스화는 base 링크 해제다(Unity 의미론): 이후
                // 편집은 자산 diff가 아니라 인라인 소유 저작이다.
                meshRenderer->m_materialBaseGuid = FileGuid{};
            }
            ImGui::EndPopup();
        }
		ImGui::DragScalar("Bitflag", ImGuiDataType_U32, &meshRenderer->m_bitflag);
        ImGui::PopStyleVar(2);
	}

    if (ImGui::CollapsingHeader("MaterialInfo", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& mat_type = Meta::Find(type_guid(Material)); // CT1: 문자열 → typeID 조회
        const auto& mat_info_type = Meta::Find(type_guid(MaterialInfomation));
        if (nullptr != meshRenderer->m_Material)
		{
			auto& mat_info = meshRenderer->m_Material->m_materialInfo;
			auto mat = meshRenderer->m_Material.get();
			TextureDropTarget(mat);

			// I5-M5 S4 — 편집 정본은 이름 기반 논리 값이다. m_materialInfo는
			// legacy 스칼라 소비자용 사본이라 binding이 함께 동기화한다.
			// meta가 없어 논리 경로가 거부되는 legacy 재질만 사본에 직접 쓴다.
			math::color baseColor = MaterialScriptBinding::GetBaseColor(*mat);
			if (ImGui::ColorEdit4("base color", &baseColor.r))
			{
				MaterialScriptBinding::SetBaseColor(*mat, baseColor);
			}

			float metallic = MaterialScriptBinding::GetFloat(*mat,
				standard_material::property::Metallic, mat_info.m_metallic);
			if (ImGui::SliderFloat("metalic", &metallic, 0.f, 1.f)
				&& !MaterialScriptBinding::SetFloat(*mat,
					standard_material::property::Metallic, metallic))
			{
				mat_info.m_metallic = metallic;
			}

			float roughness = MaterialScriptBinding::GetFloat(*mat,
				standard_material::property::Roughness, mat_info.m_roughness);
			if (ImGui::SliderFloat("roughness", &roughness, 0.f, 1.f)
				&& !MaterialScriptBinding::SetFloat(*mat,
					standard_material::property::Roughness, roughness))
			{
				mat_info.m_roughness = roughness;
			}

			// IOR 슬라이더는 은퇴 — 유일 소비자였던 TrySetMaterialInfo가 호출자
			// 0인 죽은 함수였고 PBRMaterial CB는 어떤 셰이더에도 없다. 소비 0인
			// 저작 표면은 데이터만 쌓는다. m_IOR 필드 자체는 S2c/I6에서 제거한다.
		}
		else
		{
			ImGui::Text("No Material assigned.");
		}
		for (auto& enumProp : mat_type->properties)
		{
			if (enumProp.typeID == TypeTrait::GUIDCreator::GetTypeID<MaterialRenderingMode>())
			{
				if (nullptr != meshRenderer->m_Material)
				{
					Meta::DrawEnumProperty((int*)&meshRenderer->m_Material->m_renderingMode, enumProp);
				}
				else
				{
					ImGui::Text("No Material assigned.");
				}
				break;
			}
		}
	}

	// I5-M5 S4 — ShaderMeta 선언 기반 동적 property 편집기. 편집은 논리 값
	// 경로(MaterialScriptBinding)만 탄다. 표준 3종은 위 MaterialInfo 헤더의
	// 전용 위젯이 담당하므로 건너뛴다.
	if (ImGui::CollapsingHeader("Shader Properties", ImGuiTreeNodeFlags_DefaultOpen))
	{
		Material* mat = meshRenderer->m_Material.get();
		if (nullptr == mat)
		{
			ImGui::Text("No Material assigned.");
		}
		else if (FileGuid{} == mat->m_shaderMetaGuid)
		{
			ImGui::TextUnformatted("ShaderMeta가 없어 논리 property를 편집할 수 없다");
		}
		else
		{
			std::string metaError;
			const ShaderMetaHandle metaHandle =
				DataSystems->LoadShaderMetaHandle(mat->m_shaderMetaGuid, metaError);
			const auto meta = DataSystems->ResolveShaderMeta(metaHandle);
			if (!meta)
			{
				ImGui::Text("ShaderMeta 로드 실패: %s", metaError.c_str());
			}
			else
			{
				for (const ShaderPropertyDesc& desc : meta->properties)
				{
					if (desc.name == standard_material::property::BaseColor
						|| desc.name == standard_material::property::Metallic
						|| desc.name == standard_material::property::Roughness)
					{
						continue;
					}

					// 현재 논리 값 — 없으면 ShaderMeta 기본값(정본 packer의
					// ApplyDefault). 0을 보여주면 기본 1.0 저작이 틀리게 보인다.
					MaterialPropertyValue current;
					{
						const auto authored = std::find_if(
							mat->m_propertyValues.begin(),
							mat->m_propertyValues.end(),
							[&](const MaterialPropertyValue& value)
							{
								return value.m_name == desc.name;
							});
						if (authored != mat->m_propertyValues.end())
						{
							current = *authored;
						}
						else
						{
							std::string defaultError;
							(void)MaterialPropertyPacker::ApplyDefault(desc,
								current, defaultError);
						}
					}

					switch (desc.type)
					{
					case ShaderPropertyType::Float:
					case ShaderPropertyType::Float2:
					case ShaderPropertyType::Float3:
					case ShaderPropertyType::Float4:
					{
						const int count = static_cast<int>(
							MaterialPropertyPacker::NumericElementCount(desc.type));
						float values[4]{};
						for (int i = 0; i < count && i < static_cast<int>(
							current.m_numericValue.size()); ++i)
						{
							values[i] = current.m_numericValue[i];
						}
						if (ImGui::DragScalarN(desc.name.c_str(),
							ImGuiDataType_Float, values, count, 0.01f))
						{
							(void)MaterialScriptBinding::SetFloatVector(*mat,
								*meta, desc.name, std::span<const float>(
									values, static_cast<std::size_t>(count)));
						}
						break;
					}
					case ShaderPropertyType::Int:
					{
						int value = current.m_integerValue;
						if (ImGui::DragInt(desc.name.c_str(), &value))
						{
							(void)MaterialScriptBinding::SetInt(*mat, *meta,
								desc.name, value);
						}
						break;
					}
					case ShaderPropertyType::Bool:
					{
						bool value = current.m_boolValue;
						if (ImGui::Checkbox(desc.name.c_str(), &value))
						{
							(void)MaterialScriptBinding::SetInt(*mat, *meta,
								desc.name, value ? 1 : 0);
						}
						break;
					}
					case ShaderPropertyType::Texture2D:
					{
						ImGui::Text("%s: %s", desc.name.c_str(),
							FileGuid{} == current.m_textureGuid
							? "(none)"
							: current.m_textureGuid.ToString().c_str());
						break;
					}
					default:
						ImGui::Text("%s: (Inspector 미지원 타입)",
							desc.name.c_str());
						break;
					}
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("LightMapping", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const auto& lightmap_type = Meta::Find(type_guid(LightMapping));
		Meta::TypedDraw::DrawOwnMembers(meshRenderer->m_LightMapping);
	}

	if (ImGui::CollapsingHeader("LODGroupShared", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// static 변수는 ImGui 컨텍스트 내에서 상태를 유지하기 위해 사용됩니다.
		// 여러 MeshRenderer를 동시에 편집하는 경우, 이 static 변수들을
		// 각 MeshRenderer 인스턴스에 연결된 상태로 관리해야 할 수 있습니다.
		static Mesh* selectedMesh = nullptr;
		static std::vector<float> LODThresholdsSetting; // 0.0f (Culled)는 포함하지 않음

		// 현재 선택된 Mesh가 변경되었는지 확인하고 LODThresholdsSetting을 업데이트
		if (selectedMesh != meshRenderer->m_Mesh.get())
		{
			selectedMesh = meshRenderer->m_Mesh.get();
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

	if (auto selectedMaterial =
		EditorAssetPresentation::Get().TakeSelectedMaterial())
	{
		// I5-M5 S4 — undo가 이름 재조회(FindCachedMaterial)에 기대면 캐시에
		// 없는 runtime 인스턴스로는 되돌릴 수 없다. 이전 소유를 그대로
		// 캡처한다 — shared_ptr가 수명을 보장한다.
		//
		// I5-M5 S2c-2a — 자산 재질 선택은 base 링크다: 공유 캐시 객체를 그대로
		// 물면 인스턴스 편집이 다른 renderer까지 바꾸고, 저장이 자산 연결을
		// 인라인 embed로 소실시켰다. 소유 사본 + m_materialBaseGuid로 바꾼다.
		// 링크는 GUID가 실제 standalone 재질 자산으로 해석될 때만 건다 —
		// 모델 내장 재질의 m_fileGuid는 모델 GUID라 base가 될 수 없다.
		FileGuid baseGuid{};
		if (FileGuid{} != selectedMaterial->m_fileGuid)
		{
			const file::path assetPath =
				DataSystems->GetFilePath(selectedMaterial->m_fileGuid);
			if (!assetPath.empty())
			{
				const std::shared_ptr<Material> asset =
					DataSystems->LoadMaterialShared(assetPath.stem().string());
				if (asset && asset->m_fileGuid == selectedMaterial->m_fileGuid)
				{
					baseGuid = selectedMaterial->m_fileGuid;
				}
			}
		}
		const std::shared_ptr<Material> previous = meshRenderer->m_Material;
		const FileGuid previousBase = meshRenderer->m_materialBaseGuid;
		auto ownedCopy = std::make_shared<Material>(*selectedMaterial);
		Meta::MakeCustomChangeCommand(
		[=]
		{
			meshRenderer->m_Material = previous;
			meshRenderer->m_materialBaseGuid = previousBase;
		},
		[=]
		{
			meshRenderer->m_Material = ownedCopy;
			meshRenderer->m_materialBaseGuid = baseGuid;
		});

		meshRenderer->m_Material = std::move(ownedCopy);
		meshRenderer->m_materialBaseGuid = baseGuid;
	}
}

namespace
{
	// I5-M5 S4 — 드롭의 저작 정본은 texture GUID 논리 값이다. 이름 필드는 더
	// 쓰지 않는다(D5-c가 죽인 이름 참조를 부활시키지 않는다 — 저장 시
	// SynchronizeLegacyMaterialProperties가 GUID에서 이름을 되채운다). delete는
	// GUID와 이름을 함께 비운다 — 이름만 남으면 Finalize의 이름 폴백이
	// 텍스처를 되살린다.
	void DrawMaterialTextureSlot(Material& mat, const char* emptyLabel,
		std::string_view propertyName, std::string& legacyNameField,
		const std::shared_ptr<Texture>& current, bool compress,
		const std::function<void(std::shared_ptr<Texture>)>& apply)
	{
		ImGui::PushID(propertyName.data(),
			propertyName.data() + propertyName.size());

		ImVec2 minRect;
		ImVec2 maxRect;
		if (current)
		{
			ImGui::Image((ImTextureID)EditorImGuiTexture::From(current.get()),
				ImVec2(30, 30));
			minRect = ImGui::GetItemRectMin();
			maxRect = ImGui::GetItemRectMax();

			ImGui::SameLine();
			if (ImGui::Button("delete"))
			{
				legacyNameField.clear();
				MaterialScriptBinding::SetTexture(mat, propertyName, {});
				apply({});
			}
		}
		else
		{
			ImGui::Button(emptyLabel);
			minRect = ImGui::GetItemRectMin();
			maxRect = ImGui::GetItemRectMax();
		}

		ImRect bb(minRect, maxRect);
		if (ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget")))
		{
			if (const ImGuiPayload* payload =
				ImGui::AcceptDragDropPayload("Texture"))
			{
				const char* droppedFilePath = (const char*)payload->Data;
				file::path filename = droppedFilePath;
				file::path filepath =
					PathFinder::Relative("Textures\\") / filename.filename();
				if (filename.filename().empty())
				{
					Debug->Log("Empty Texture File Name");
				}
				else if (const FileGuid guid = DataSystems->GetFileGuid(filepath);
					FileGuid{} == guid)
				{
					// GUID 없는 드롭을 받으면 화면에는 보여도 저장이 안 된다 —
					// 조용한 소실보다 거부가 낫다.
					Debug->LogWarning("드롭한 텍스처에 .meta GUID가 없다 — "
						"저작을 거부한다: " + filepath.string());
				}
				else
				{
					apply(DataSystems->LoadSharedMaterialTexture(
						filepath.string(), compress));
					MaterialScriptBinding::SetTexture(mat, propertyName, guid);
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::PopID();
	}
}

void TextureDropTarget(Material* mat)
{
	ImGui::PushID(mat);
	DrawMaterialTextureSlot(*mat, "No basemap texture",
		standard_material::property::BaseColorMap, mat->m_baseColorTexName,
		mat->GetBaseColorMapShared(), true,
		[mat](std::shared_ptr<Texture> texture)
		{
			mat->UseBaseColorMap(std::move(texture));
		});
	DrawMaterialTextureSlot(*mat, "No Normalmap texture",
		standard_material::property::NormalMap, mat->m_normalTexName,
		mat->GetNormalMapShared(), false,
		[mat](std::shared_ptr<Texture> texture)
		{
			mat->UseNormalMap(std::move(texture));
		});
	DrawMaterialTextureSlot(*mat, "No ORMmap texture",
		standard_material::property::OrmMap, mat->m_ORM_TexName,
		mat->GetOccRoughMetalMapShared(), false,
		[mat](std::shared_ptr<Texture> texture)
		{
			mat->UseOccRoughMetalMap(std::move(texture));
		});
	ImGui::PopID();
}
