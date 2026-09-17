#include "ExternUI.h"
#include "EditorSessionState.h"
#include "DataSystem.h"
#include "EditorAssetDragPayload.h"
#include "Assets/ModelAssetGeneration.h" // MBC9
#include "EditorImGuiTexture.h"
#include "Terrain.h"
#include "FileDialog.h"
#include "FoliageComponent.h"
#include "EditorIcons.h"
#include "EditorObjectOperations.h"
#include "EditorPropertyRow.h"
#include "EditorTheme.h"
#include "imgui_stdlib.h"
#include <initializer_list>


namespace
{
	// 전용 드로어의 줄 배치 상태 (PHASE 21 W2-I4). 인스펙터는 한 스레드에서 그린다.
	editor::widgets::property_layout_state g_terrainLayout{};

	// 한 줄을 버튼 여럿이 같은 폭으로 나눠 쓴다. 눌린 버튼의 순번, 없으면 -1.
	int ButtonRow(std::initializer_list<const char*> labels)
	{
		const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
		const int count = static_cast<int>(labels.size());
		const float width = ImMax(1.f,
			(ImGui::GetContentRegionAvail().x - gap * static_cast<float>(count - 1)) / static_cast<float>(count));
		int pressed = -1;
		int index = 0;
		for (const char* label : labels)
		{
			if (index > 0) ImGui::SameLine(0.f, gap);
			if (ImGui::Button(label, ImVec2(width, 0.f))) pressed = index;
			++index;
		}
		return pressed;
	}

	// 브러시 마스크 목록과 불러오기. 페인트·폴리지 두 모드가 같은 것을 그렸다 —
	// 선택 표시는 브러시 하나에 붙으므로 한 자리에 둔다.
	void DrawBrushMasks(TerrainComponent* terrainComponent, TerrainBrush* brush)
	{
		if (ImGui::Button("Load Mask Texture", ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
		{
			file::path maskTexture = ShowOpenFileDialog(L"");
			terrainComponent->SetBrushMaskTexture(brush, maskTexture);
		}
		if (brush->m_masks.empty()) return;

		static int selectedMaskIndex = -1;
		std::vector<std::string>& maskNames = brush->GetMaskNames();
		const float tile = editor::ThemePixels(64.f);
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		for (int maskIndex = 0; maskIndex < static_cast<int>(brush->m_masks.size()); ++maskIndex)
		{
			// 폭이 허락하는 만큼 한 줄에 놓는다.
			if (maskIndex > 0 && ImGui::GetItemRectMax().x + gap + tile <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
				ImGui::SameLine(0.f, gap);
			ImGui::PushID(maskIndex);
			const char* name = maskIndex < static_cast<int>(maskNames.size()) ? maskNames[maskIndex].c_str() : "";
			if (ImGui::Button((std::string(name) + "###Mask").c_str(), ImVec2(tile, tile)))
			{
				if (selectedMaskIndex != maskIndex)
				{
					selectedMaskIndex = maskIndex;
					brush->SetMaskID(maskIndex); // 선택된 마스크 ID 설정
				}
				else
				{
					selectedMaskIndex = -1; // 이미 선택된 마스크를 다시 클릭하면 선택 해제
					brush->SetMaskID(0xFFFFFFFF); // No mask selected
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name);
			ImGui::PopID();
		}
	}
}

void ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent)
{
    if (EditorObjectOperations::IsEditLocked(terrainComponent->GetOwner(), true))
    {
        if (auto* brush = EditorSessionState::Get().FindTerrainBrush()) brush->m_isEditMode = false;
        ImGui::TextDisabled("Terrain editing is locked.");
        return;
    }
	TerrainBrush* g_CurrentBrush = terrainComponent->GetCurrentBrush();
	TerrainBrush* sessionBrush = EditorSessionState::Get().FindTerrainBrush();

	if (!g_CurrentBrush && !sessionBrush)
	{
		Debug::PrintLog(spdlog::level::err, "TerrainComponent::GetCurrentBrush() returned nullptr.");
		sessionBrush = &EditorSessionState::Get().GetOrCreateTerrainBrush();
		terrainComponent->SetTerrainBrush(sessionBrush);
		return;
	}
	else if (!g_CurrentBrush)
	{
		g_CurrentBrush = sessionBrush;
	}

	const editor::widgets::property_sheet sheet(g_terrainLayout, { "Width", "Height", "Edit Mode",
		"Radius", "Strength", "Flat Height", "Layer Name", "Tiling", "Type", "Density", "Model", "Action" });

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

	ImGui::SeparatorText("Terrain Size");
	ImGui::SetNextItemWidth(sheet.line("Width"));
	ImGui::InputInt("##Width", &editWidth);
	ImGui::SetNextItemWidth(sheet.line("Height"));
	ImGui::InputInt("##Height", &editHeight);

	editWidth = editWidth > 2 ? editWidth : 2;
	editHeight = editHeight > 2 ? editHeight : 2;

	if (prewidth != editWidth || preheight != editHeight)
	{
		terrainComponent->Resize(editWidth, editHeight);
	}

	g_CurrentBrush->m_isEditMode = false;
	if (!ImGui::CollapsingHeader("Terrain Editor", ImGuiTreeNodeFlags_DefaultOpen)) return;

	//inspector가 화면에 뜨는 경우 브러시가 활성화된 상태로 설정
	g_CurrentBrush->m_isEditMode = true;
	const char* modes[] = { "Raise", "Lower", "Flatten", "PaintLayer", "FoliageMode" };
	int currentMode = static_cast<int>(g_CurrentBrush->m_mode);
	ImGui::SetNextItemWidth(sheet.line("Edit Mode"));
	if (ImGui::Combo("##EditMode", &currentMode, modes, IM_ARRAYSIZE(modes)))
		g_CurrentBrush->m_mode = static_cast<TerrainBrush::Mode>(currentMode);

	if (g_CurrentBrush->m_mode != TerrainBrush::Mode::FoliageMode)
	{
		if (!ImGui::CollapsingHeader("Paint Terrain", ImGuiTreeNodeFlags_DefaultOpen)) return;

		ImGui::SetNextItemWidth(sheet.line("Radius"));
		ImGui::SliderFloat("##Radius", &g_CurrentBrush->m_radius, 1.0f, 50.0f);
		ImGui::SetNextItemWidth(sheet.line("Strength"));
		ImGui::SliderFloat("##Strength", &g_CurrentBrush->m_strength, 0.0f, 1.0f);

		// Flatten 옵션일 때만 목표 높이 입력
		if (g_CurrentBrush->m_mode == TerrainBrush::Mode::Flatten)
		{
			ImGui::SetNextItemWidth(sheet.line("Flat Height"));
			ImGui::SliderFloat("##FlatHeight", &g_CurrentBrush->m_flatTargetHeight, -100.0f, 500.0f);
		}

		// [MODIFIED] 통합된 레이어 관리 UI
		if (g_CurrentBrush->m_mode == TerrainBrush::Mode::PaintLayer)
		{
			ImGui::SeparatorText("Layers");

			const bool isLayerSelected = terrainComponent->GetSelectedLayerId() != 0xFFFFFFFF;
			const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
			const float half = ImMax(1.f, (ImGui::GetContentRegionAvail().x - gap) * 0.5f);
			if (ImGui::Button(EditorIcon::Label<EditorIcon::Add, " Add">, ImVec2(half, 0.f)))
			{
				file::path diffuseFile = ShowOpenFileDialog(L"");
				if (!diffuseFile.empty())
				{
					std::wstring diffuseFileName = diffuseFile.filename();
					terrainComponent->AddLayer(diffuseFile, diffuseFileName, 10.0f);
				}
			}
			ImGui::SameLine(0.f, gap);
			// 선택된 레이어가 없을 경우 삭제 버튼 비활성화
			ImGui::BeginDisabled(!isLayerSelected);
			if (ImGui::Button(EditorIcon::Label<EditorIcon::Delete, " Remove">, ImVec2(half, 0.f)) && isLayerSelected)
			{
				uint32_t layerToDelete = terrainComponent->GetSelectedLayerId();
				// 삭제 후 선택 해제
				terrainComponent->SetSelectedLayerId(0xFFFFFFFF);
				g_CurrentBrush->m_layerID = 0; // 브러시 타겟 초기화
				terrainComponent->RemoveLayer(layerToDelete);
			}
			ImGui::EndDisabled();

			// --- 레이어 목록 리스트 박스 ---
			std::vector<const char*> layerNames = terrainComponent->GetLayerNames();
			int currentSelection = static_cast<int>(terrainComponent->GetSelectedLayerId());
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
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
					// 백엔드를 묻지 않는다 — Texture::HasImage 주석 참고(T2).
					if (selectedLayer->diffuseTexture && selectedLayer->diffuseTexture->HasImage())
					{
						const float thumbnail = editor::ThemePixels(64.f);
						ImGui::Image((ImTextureID)EditorImGuiTexture::From(selectedLayer->diffuseTexture), ImVec2(thumbnail, thumbnail));
					}

					std::string layerName = selectedLayer->layerName;
					ImGui::SetNextItemWidth(sheet.line("Layer Name"));
					ImGui::InputText("##LayerName", &layerName, ImGuiInputTextFlags_ReadOnly);

					// 타일링 수정
					float tiling = selectedLayer->tilling;
					ImGui::SetNextItemWidth(sheet.line("Tiling"));
					if (ImGui::DragFloat("##Tiling", &tiling, 0.1f, 0.1f, 4096.0f))
					{
						selectedLayer->tilling = tiling;
						terrainComponent->UpdateLayerDesc();
					}
				}
			}
		}

		if (ImGui::Button("Refresh Terrain Texture", ImVec2(ImGui::GetContentRegionAvail().x, 0.f))) {
			terrainComponent->RefreshTexture();
		}

		// 브러시 모양 선택
		DrawBrushMasks(terrainComponent, g_CurrentBrush);

		ImGui::SeparatorText("Terrain Asset");
		switch (ButtonRow({ "Save Terrain", "Load Terrain" }))
		{
		case 0:
		{
			file::path savePath = ShowSaveFileDialog(L"", L"Save File", PathFinder::Relative("Terrain\\"));
			if (savePath != L"") {
				std::wstring folderPath = savePath.parent_path().wstring();
				std::wstring fileName = savePath.stem().wstring();
				terrainComponent->Save(folderPath, fileName);
			}
			break;
		}
		case 1:
		{
			file::path loadPath = ShowOpenFileDialog(L"");
			if (!loadPath.empty())
			{
				terrainComponent->Load(loadPath);
			}
			break;
		}
		default:
			break;
		}
		return;
	}

	if (!ImGui::CollapsingHeader("Paint Foliage", ImGuiTreeNodeFlags_DefaultOpen)) return;

	g_CurrentBrush->m_isEditMode = true;
	Entity* owner = terrainComponent->GetOwner();
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
		ImGui::SetNextItemWidth(sheet.line("Type"));
		ImGui::Combo("##FoliageType", &typeIndex, typeNames.data(), static_cast<int>(typeNames.size()));
		g_CurrentBrush->m_foliageTypeID = static_cast<uint32_t>(typeIndex);
	}
	ImGui::SetNextItemWidth(sheet.line("Radius"));
	ImGui::SliderFloat("##FoliageRadius", &g_CurrentBrush->m_radius, 1.0f, 50.0f);
	ImGui::SetNextItemWidth(sheet.line("Density"));
	ImGui::InputInt("##Density", &g_CurrentBrush->m_foliageDensity);

	ImGui::SeparatorText("Foliage Mesh");
	ImGui::Button("Drag model here###FoliageModel", ImVec2(sheet.line("Model"), 0.f));
	if (!(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Model"))
		{
			const file::path filepath = editor::asset_drag::path_of(*payload);
			// 폴리지는 모델을 stem 으로 저장하고 GetStemToGuid 로 다시 찾는다.
			// 그 stem 이 끌어 온 파일로 돌아오지 않으면(같은 이름이 다른 폴더에
			// 있으면) 다른 모델이 묶이므로 받지 않는다.
			const FileGuid droppedGuid = DataSystems->GetFileGuid(filepath);
			if (droppedGuid == nullFileGuid
				|| DataSystems->GetStemToGuid(filepath.stem().string()) != droppedGuid)
			{
				Debug::PrintLog(spdlog::level::err, "Foliage model drop: '" + filepath.string()
					+ "' is not the model its name resolves to — rename it or remove the duplicate");
			}
			else if (auto generation = DataSystems->LoadModelAssetGenerationByPath(filepath.string()))
			{
				// MBC9 — Foliage 자산은 모델을 이름(stem)으로 적고, 런타임 바인딩은
				// AddFoliageType(BindModelGeneration)이 잇는다.
				FoliageType type(generation->SourcePath().stem().string(), true);
				foliage->AddFoliageType(type);
				g_CurrentBrush->m_foliageTypeID = static_cast<uint32_t>(foliage->GetFoliageTypes().size() - 1);
			}
		}
		ImGui::EndDragDropTarget();
	}
	int foliageMode = static_cast<int>(g_CurrentBrush->m_foliageMode);
	const char* fModes[] = { "Paint", "Erase" };
	ImGui::SetNextItemWidth(sheet.line("Action"));
	if (ImGui::Combo("##FoliageAction", &foliageMode, fModes, 2))
	{
		g_CurrentBrush->m_foliageMode = static_cast<TerrainBrush::FoliageMode>(foliageMode);
	}

	ImGui::SeparatorText("Foliage Asset");
	if (ImGui::Button("Save Foliage", ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
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
		// 취소하면 빈 경로가 돌아온다. 예전에는 그대로 넘겨 확장자만
		// 붙은 ".foliage" 파일이 실행 폴더에 생겼다. 바로 위 Save
		// Terrain과 같이 지역 가드로 거른다 — 여기서 함수를 return하면
		// 같은 프레임의 남은 브러시 UI가 통째로 빠진다.
		if (!savePath.empty())
		{
			file::path assetName = savePath.filename();
			if (0 == _wcsicmp(assetName.extension().c_str(), L".foliage"))
			{
				assetName = assetName.stem();
			}

			const file::path directory = savePath.has_parent_path()
				? savePath.parent_path() : foliageDir;
			foliage->SaveFoliageAsset(directory, assetName.wstring());
		}
	}

	// 브러시 모양 선택
	DrawBrushMasks(terrainComponent, g_CurrentBrush);
}
