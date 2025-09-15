#include "ExternUI.h"
#include "DeviceState.h"
#include "Terrain.h"
#include "TableAPIHelper.h"
#include "FileDialog.h"
#include "CustomCollapsingHeader.h"
#include "FoliageComponent.h"
#include "IconsFontAwesome6.h"
#include "fa.h"


void ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent)
{
	TerrainBrush* g_CurrentBrush = terrainComponent->GetCurrentBrush();

	if (!g_CurrentBrush && !EngineSettingInstance->terrainBrush)
	{
		Debug->LogError("TerrainComponent::GetCurrentBrush() returned nullptr.");
		EngineSettingInstance->terrainBrush = new TerrainBrush();
		terrainComponent->SetTerrainBrush(EngineSettingInstance->terrainBrush);
		return;
	}
	else if (!g_CurrentBrush)
	{
		g_CurrentBrush = EngineSettingInstance->terrainBrush;
	}

	int prewidth = terrainComponent->m_width;
	int preheight = terrainComponent->m_height;
	int editWidth = terrainComponent->m_width;
	int editHeight = terrainComponent->m_height;
	file::path terrainAssetPath = terrainComponent->m_terrainTargetPath;
	FileGuid& terrainAssetGuid = terrainComponent->m_trrainAssetGuid;
	if (terrainAssetGuid == nullFileGuid && !terrainAssetPath.empty())
	{
		terrainAssetGuid = DataSystems->GetFileGuid(terrainAssetPath);
	}

	//bool change_edit = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SeparatorText("Terrain Size");
	if (ImGui::InputInt("Width", &editWidth)) {
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (editWidth < 2)
			{
				editWidth = 2;
			}
		}
	}
	if (ImGui::InputInt("Height", &editHeight)) {
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (editHeight < 2)
			{
				editHeight = 2;
			}
		}
	}

	editWidth = editWidth > 2 ? editWidth : 2;
	editHeight = editHeight > 2 ? editHeight : 2;

	if (prewidth != editWidth || preheight != editHeight)
	{
		terrainComponent->Resize(editWidth, editHeight);
	}

	EngineSettingInstance->terrainBrush->m_isEditMode = false;
	// ImGui UI 예시 (에디터 툴 패널 내부)
	if (ImGui::CollapsingHeader("Terrain Editor", ImGuiTreeNodeFlags_DefaultOpen))
	{
		//inspector가 화면에 뜨는 경우 브러시가 활성화된 상태로 설정
		EngineSettingInstance->terrainBrush->m_isEditMode = true; // 브러시가 활성화된 상태로 설정
		// 모드 선택
		const char* modes[] = { "Raise", "Lower", "Flatten", "PaintLayer", "FoliageMode" };
		int currentMode = static_cast<int>(g_CurrentBrush->m_mode);
		if (ImGui::Combo("Edit Mode", &currentMode, modes, IM_ARRAYSIZE(modes)))
			g_CurrentBrush->m_mode = static_cast<TerrainBrush::Mode>(currentMode);

		if (g_CurrentBrush->m_mode != TerrainBrush::Mode::FoliageMode)
		{
			if (ImGui::CollapsingHeader("Paint Terrain", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// 반지름 슬라이더 (1 ~ 50 등의 범위 예시)
				ImGui::SliderFloat("Radius", &g_CurrentBrush->m_radius, 1.0f, 50.0f);

				// 세기 슬라이더
				ImGui::SliderFloat("Strength", &g_CurrentBrush->m_strength, 0.0f, 1.0f);

				// Flatten 옵션일 때만 목표 높이 입력
				if (g_CurrentBrush->m_mode == TerrainBrush::Mode::Flatten)
				{
					//ImGui::InputFloat("Target Height", &g_CurrentBrush->m_flatTargetHeight);
					ImGui::SliderFloat("FlatHeight", &g_CurrentBrush->m_flatTargetHeight, -100.0f, 500.0f);
				}

				// [MODIFIED] 통합된 레이어 관리 UI
				if (g_CurrentBrush->m_mode == TerrainBrush::Mode::PaintLayer)
				{
					ImGui::SeparatorText("Layers");

					// --- 추가/삭제 버튼 ---
					if (ImGui::Button(ICON_FA_PLUS " Add"))
					{
						file::path diffuseFile = ShowOpenFileDialog(L"");
						if (!diffuseFile.empty())
						{
							std::wstring diffuseFileName = diffuseFile.filename();
							terrainComponent->AddLayer(diffuseFile, diffuseFileName, 10.0f);
						}
					}
					ImGui::SameLine();

					// 선택된 레이어가 없을 경우 삭제 버튼 비활성화
					bool isLayerSelected = terrainComponent->GetSelectedLayerId() != 0xFFFFFFFF;
					if (!isLayerSelected)
					{
						ImGui::BeginDisabled();
					}
					if (ImGui::Button(ICON_FA_TRASH_CAN " Remove"))
					{
						if (isLayerSelected)
						{
							uint32_t layerToDelete = terrainComponent->GetSelectedLayerId();
							// 삭제 후 선택 해제
							terrainComponent->SetSelectedLayerId(0xFFFFFFFF);
							g_CurrentBrush->m_layerID = 0; // 브러시 타겟 초기화
							terrainComponent->RemoveLayer(layerToDelete);
						}
					}
					if (!isLayerSelected)
					{
						ImGui::EndDisabled();
					}

					// --- 레이어 목록 리스트 박스 ---
					std::vector<const char*> layerNames = terrainComponent->GetLayerNames();
					int currentSelection = static_cast<int>(terrainComponent->GetSelectedLayerId());

					if (ImGui::ListBox("##LayerList", &currentSelection, layerNames.data(), (int)layerNames.size(), 4))
					{
						if (currentSelection >= 0 && currentSelection < layerNames.size())
						{
							// 관리용 선택과 브러시 페인트 타겟을 동시 업데이트
							terrainComponent->SetSelectedLayerId(static_cast<uint32>(currentSelection));
							g_CurrentBrush->m_layerID = currentSelection;
						}
					}

					// --- 선택된 레이어 속성 편집 ---
					if (terrainComponent->GetSelectedLayerId() != 0xFFFFFFFF)
					{
						TerrainLayer* selectedLayer = terrainComponent->GetLayerDesc(terrainComponent->GetSelectedLayerId());
						if (selectedLayer)
						{
							ImGui::SeparatorText("Properties");

							// 텍스처 썸네일 표시
							if (selectedLayer->diffuseTexture && selectedLayer->diffuseTexture->m_pSRV)
							{
								ImGui::Image((ImTextureID)selectedLayer->diffuseTexture->m_pSRV, ImVec2(64, 64));
								ImGui::SameLine();
							}
							
							ImGui::Text("Name: %s", selectedLayer->layerName.c_str());

							// 타일링 수정
							float tiling = selectedLayer->tilling;
							if (ImGui::DragFloat("Tiling", &tiling, 0.1f, 0.1f, 4096.0f))
							{
								selectedLayer->tilling = tiling;
								terrainComponent->UpdateLayerDesc();
							}
						}
					}
				}

				// 브러시 모양 선택
				if (ImGui::Button("mask texture load"))
				{
					file::path maskTexture = ShowOpenFileDialog(L"");
					terrainComponent->SetBrushMaskTexture(g_CurrentBrush, maskTexture);
				}

				if (!g_CurrentBrush->m_masks.empty())
				{
					std::vector<std::string>& maskNameVec = g_CurrentBrush->GetMaskNames();
					std::vector<const char*> maskNames;
					maskNames.reserve(maskNameVec.size());
					for (const auto& name : maskNameVec) {
						maskNames.push_back(name.c_str());
					}

					static int selectedMaskIndex = -1;
					int maskIndex = 0;
					for (const auto& mask : g_CurrentBrush->m_masks)
					{
						if (ImGui::ImageButton(maskNames[maskIndex], (ImTextureID)mask.m_maskSRV, ImVec2((float)100.0f, (float)100.0f)))
						{
							if (selectedMaskIndex != maskIndex)
							{
								selectedMaskIndex = maskIndex;
								uint32_t id = static_cast<uint32_t>(maskIndex);
								g_CurrentBrush->SetMaskID(maskIndex); // 선택된 마스크 ID 설정
							}
							else
							{
								selectedMaskIndex = -1; // 이미 선택된 마스크를 다시 클릭하면 선택 해제
								uint32_t id = 0xFFFFFFFF; // "None" 선택 시 -1로 설정
								g_CurrentBrush->SetMaskID(id); // No mask selected
							}
						}
						maskIndex++;
						/*ImGui::Image((ImTextureID)mask.m_maskSRV,
							ImVec2((float)100.0f, (float)100.0f));*/
					}
				}

				ImGui::SeparatorText("Terrain Asset");
				//save , load
				if (ImGui::Button("Save Terrain"))
				{
					file::path savePath = ShowSaveFileDialog(L"", L"Save File", PathFinder::Relative("Terrain\\"));
					if (savePath != L"") {
						std::wstring folderPath = savePath.parent_path().wstring();
						std::wstring fileName = savePath.stem().wstring();
						terrainComponent->Save(folderPath, fileName);
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Load Terrain"))
				{
					//todo:
					file::path loadPath = ShowOpenFileDialog(L"");
					if (!loadPath.empty())
					{
						terrainComponent->Load(loadPath);
					}
				}
			}
		}
		else
		{
			if (ImGui::CollapsingHeader("Paint Foliage", ImGuiTreeNodeFlags_DefaultOpen))
			{
				EngineSettingInstance->terrainBrush->m_isEditMode = true;
				GameObject* owner = terrainComponent->GetOwner();
				FoliageComponent* foliage = owner->GetComponent<FoliageComponent>();
				if (!foliage)
				{
					foliage = owner->AddComponent<FoliageComponent>();
				}

				std::vector<const char*> typeNames;
				for (const auto& t : foliage->GetFoliageTypes()) { typeNames.push_back(t.m_modelName.c_str()); }
				int typeIndex = static_cast<int>(g_CurrentBrush->m_foliageTypeID);
				if (!typeNames.empty())
				{
					ImGui::Combo("Type", &typeIndex, typeNames.data(), static_cast<int>(typeNames.size()));
					g_CurrentBrush->m_foliageTypeID = static_cast<uint32_t>(typeIndex);
				}
				// 반지름 슬라이더 (1 ~ 50 등의 범위 예시)
				ImGui::SliderFloat("Radius", &g_CurrentBrush->m_radius, 1.0f, 50.0f);
				// 세기 슬라이더
				ImGui::InputInt("Density", &g_CurrentBrush->m_foliageDensity);

				ImGui::SeparatorText("Foliage Mesh");
				ImGui::Text("Drag Model Here");
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Model"))
					{
						const char* droppedFilePath = static_cast<const char*>(payload->Data);
						file::path filename = file::path(droppedFilePath).filename();
						file::path filepath = PathFinder::Relative("Models\\") / filename;
						if (Model* model = DataSystems->LoadCashedModel(filepath.string().c_str()))
						{
							FoliageType type(model, true);
							foliage->AddFoliageType(type);
							g_CurrentBrush->m_foliageTypeID = static_cast<uint32_t>(foliage->GetFoliageTypes().size() - 1);
						}
					}
					ImGui::EndDragDropTarget();
				}
				int foliageMode = static_cast<int>(g_CurrentBrush->m_foliageMode);
				const char* fModes[] = { "Paint", "Erase" };
				if (ImGui::Combo("Action", &foliageMode, fModes, 2))
				{
					g_CurrentBrush->m_foliageMode = static_cast<TerrainBrush::FoliageMode>(foliageMode);
				}

				ImGui::SeparatorText("Foliage Asset");
				if (ImGui::Button("Save Foliage"))
				{
					file::path foliageDir = PathFinder::Relative("Foliage\\");
					if (!file::exists(foliageDir))
					{
						if (!file::create_directories(foliageDir))
						{
							std::cerr << "Failed to create Foliage directory." << std::endl;
							return;
						}
					}

					file::path savePath = ShowSaveFileDialog(L"", L"Save File", foliageDir);
					foliage->SaveFoliageAsset(savePath);
				}

				// 브러시 모양 선택
				if (ImGui::Button("mask texture load"))
				{
					file::path maskTexture = ShowOpenFileDialog(L"");
					terrainComponent->SetBrushMaskTexture(g_CurrentBrush, maskTexture);
				}

				if (!g_CurrentBrush->m_masks.empty())
				{
					std::vector<std::string>& maskNameVec = g_CurrentBrush->GetMaskNames();
					std::vector<const char*> maskNames;
					maskNames.reserve(maskNameVec.size());
					for (const auto& name : maskNameVec) {
						maskNames.push_back(name.c_str());
					}

					static int selectedMaskIndex = -1;
					int maskIndex = 0;
					for (const auto& mask : g_CurrentBrush->m_masks)
					{
						if (ImGui::ImageButton(maskNames[maskIndex], (ImTextureID)mask.m_maskSRV, ImVec2((float)100.0f, (float)100.0f)))
						{
							if (selectedMaskIndex != maskIndex)
							{
								selectedMaskIndex = maskIndex;
								uint32_t id = static_cast<uint32_t>(maskIndex);
								g_CurrentBrush->SetMaskID(maskIndex); // 선택된 마스크 ID 설정
							}
							else
							{
								selectedMaskIndex = -1; // 이미 선택된 마스크를 다시 클릭하면 선택 해제
								uint32_t id = 0xFFFFFFFF; // "None" 선택 시 -1로 설정
								g_CurrentBrush->SetMaskID(id); // No mask selected
							}
						}
						maskIndex++;
						/*ImGui::Image((ImTextureID)mask.m_maskSRV,
							ImVec2((float)100.0f, (float)100.0f));*/
					}
				}
			}
		}
	}
}