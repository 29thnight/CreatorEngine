#include "EditorModelPlacement.h"
#include "EditorTheme.h"
#include "EditorObjectOperations.h"
#include "EditorAssetDragPayload.h"
#include "EditorAssetPresentation.h"
#include "EditorImGuiTexture.h"
#include "HierarchyWindow.h"
#include "EditorPanelCost.h"
#include "EditorWindowNames.h"
#include "Windows/EditorStandardWindows.h"
#include "EditorMenuDraw.h"
#include "EditorMenuTargets.h"
#include "ReflectionUndo.h"
#include "SpriteRenderer.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "Entity.h"
#include "LightComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "CameraComponent.h"
#include "UIManager.h"
#include "DataSystem.h"
#include "PathFinder.h"
#include "RectTransformComponent.h"
#include "SpriteSheetComponent.h"
#include "GameObjectCommand.h"
#include "PrefabEditor.h"
#include "EditorIcons.h"
#include "InputManager.h"
#include "MetaStateCommand.h"
#include "ReflectionRegister.h"

namespace
{
	// 창 상태의 유일한 자리(PHASE 21 W3). 검색어·클립보드·스크롤 요청 셋뿐이다.
	HierarchyWindow& hierarchy_state()
	{
		static HierarchyWindow value;
		return value;
	}

	// S&Box Hierarchy의 줄 규칙(PHASE 21). 한 줄은 theme의 RowHeight 한 칸을
	// 통째로 차지하고, 줄끼리 맞닿으며, 홀짝으로 바탕이 갈린다.
	//
	// FramePadding 표지가 없으면 ImGui는 트리 노드의 높이를 글자 높이(16px)로
	// 잡는다 — 줄 간격 8px은 줄 사이의 빈틈으로 남아 띠가 끊긴다. 이 표지를
	// 붙여야 한 줄의 상자가 RowHeight(24px)가 되고, Draw가 구간에 거는
	// ItemSpacing.y = 0과 맞물려 줄이 서로 맞닿는다.
	constexpr ImGuiTreeNodeFlags kRowFlags =
		ImGuiTreeNodeFlags_SpanFullWidth |
		ImGuiTreeNodeFlags_FramePadding |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_OpenOnDoubleClick;

	ImVec4 hierarchy_color(std::uint32_t rgb) noexcept
	{
		return ImVec4(((rgb >> 16) & 255) / 255.f,
			((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1.f);
	}

	// 한 줄의 가로 범위는 SpanFullWidth가 쓰는 것과 같은 WorkRect다 — 띠와
	// 선택 하이라이트의 양 끝이 어긋나지 않는다.
	bool row_rect(ImVec2& rowMin, ImVec2& rowMax) noexcept
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (!window || window->SkipItems) return false;

		const float top = window->DC.CursorPos.y;
		rowMin = ImVec2(window->WorkRect.Min.x, top);
		rowMax = ImVec2(window->WorkRect.Max.x, top + ImGui::GetFrameHeight());
		return true;
	}

	// 줄 바탕은 노드보다 **먼저** 칠한다. 선택·hover 하이라이트는 ImGui가
	// 그 위에 그리므로 다시 덮이지 않는다.
	void draw_row_band(int rowIndex)
	{
		ImVec2 rowMin{}, rowMax{};
		if (0 == (rowIndex & 1) || !row_rect(rowMin, rowMax)) return;

		ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax,
			ImGui::GetColorU32(hierarchy_color(editor::HierarchyThemeTokens::AlternateRow)));
	}

	// 씬 이름과 [ Dont Destroy On Load ]는 묶음의 머리다 — 홀짝 띠 대신 한 단
	// 위의 바탕으로 칠해 아래 엔티티 줄과 갈라 둔다.
	bool group_tree_node(const char* label)
	{
		ImVec2 rowMin{}, rowMax{};
		if (row_rect(rowMin, rowMax))
		{
			ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax,
				ImGui::GetColorU32(editor::ThemeColorValue(editor::ThemeColor::Panel)));
		}
		// NoTreePushOnOpen: 들여쓰기는 평탄 목록의 depth 가 준다(W7-2). ImGui 의
		// TreePush 로 깊이를 쌓으면 목록을 인덱스로 끊어 그릴 수 없다.
		return ImGui::TreeNodeEx(label,
			kRowFlags | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen);
	}

	// Neutral selection keeps the blue object label readable; hover stays selected.
	int push_selected_row_colors(bool isSelected)
	{
		if (!isSelected) return 0;

		using T = editor::HierarchyThemeTokens;
		ImGui::PushStyleColor(ImGuiCol_Header, hierarchy_color(T::SelectedRow));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hierarchy_color(T::SelectedHoveredRow));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, hierarchy_color(T::SelectedRow));
		return 3;
	}

	// The toolbar and context menu use the same creation operations and Undo path.
	void draw_creation_menu(Scene* scene)
	{
		if (ImGui::MenuItem("Create Empty", "Ctrl + Shift + N"))
			EditorObjectOperations::Create(scene, "Entity", GameObjectType::Empty);

		const auto create_light = [scene](const char* name, LightType type)
		{
			auto creation = EditorObjectOperations::Create(scene, name, GameObjectType::Light);
			auto* obj = creation.IsSuccess()
				? scene->TryGetEntity(static_cast<Entity::Index>(creation.data.Find("index")->AsInt()))
				: nullptr;
			if (!obj) return;
			if (type == LightType::SpotLight)
				obj->Transform_().SetRotation({ 0.7, 0, 0, 1 }, TransformWriteReason::Inspector);
			if (auto* light = obj->GetComponent<LightComponent>())
			{
				light->SetLightType(type);
				light->m_lightStatus = LightStatus::Enabled;
			}
		};
		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional Light"))
				create_light("Directional Light", LightType::DirectionalLight);
			if (ImGui::MenuItem("Point Light"))
				create_light("Point Light", LightType::PointLight);
			if (ImGui::MenuItem("Spot Light"))
				create_light("Spot Light", LightType::SpotLight);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Camera"))
			EditorObjectOperations::Create(scene, "Camera", GameObjectType::Camera);
		if (ImGui::BeginMenu("UI"))
		{
			if (ImGui::MenuItem("Image")) UIManagers->MakeImage("NoneImage", nullptr);
			if (ImGui::MenuItem("Text")) UIManagers->MakeText("Text", "null", nullptr);
			ImGui::MenuItem("Button", nullptr, false, false);
			ImGui::EndMenu();
		}
	}
}

void editor::windows::draw_hierarchy()
{
	// W7-0: 이 패널이 프레임에서 쓰는 시간과 그린 행 수를 센다. 캐시의 이득은
	// 시간만이 아니라 **안 하게 된 일의 수**로도 보여야 한다.
	const panel_cost_scope cost{ panel_cost_slot::hierarchy };
	hierarchy_state().Draw();
}

void HierarchyWindow::DrawSceneObjectRow(Entity* obj, const editor::hierarchy_flat_row& row)
{
	auto scene = SceneManagers->GetActiveScene();
	auto& selectedObjects = scene->m_selectedEntities;

	// 걸러내기(검색)와 펼침은 목록이 이미 정했다 — 여기서는 한 줄만 그린다.
	ImGuiTreeNodeFlags flags = kRowFlags | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	bool isSelected = std::find(selectedObjects.begin(), selectedObjects.end(), obj) != selectedObjects.end() || scene->m_selectedEntity == obj;
	if (isSelected)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	if (!row.hasChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

	// 펼침의 정본은 목록이다(HierarchyFlatten.h). ImGui 에게 기억을 맡기면
	// clipping 이 건너뛴 노드의 상태를 잃는다.
	ImGui::SetNextItemOpen(row.expanded, ImGuiCond_Always);

	const bool isDisabled = !obj->IsEnabled();
	ImGui::PushStyleColor(ImGuiCol_Text, isDisabled
		? editor::ThemeColorValue(editor::ThemeColor::TextDisabled)
		: hierarchy_color(isSelected ? editor::HierarchyThemeTokens::SelectedText
			: editor::HierarchyThemeTokens::EntityText));

    const std::string id = "##entity" + std::to_string(obj->m_index);
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float iconSize = ImGui::GetFontSize();
    const ImVec2 imageMin{rowStart.x + ImGui::GetTreeNodeToLabelSpacing(),
        rowStart.y + (ImGui::GetFrameHeight() - iconSize) * .5f};
    draw_row_band(row.bandIndex);
    ++m_rowIndex;
    const int rowColors = push_selected_row_colors(isSelected);
    ImGui::TreeNodeEx(id.c_str(), flags);
    ImGui::PopStyleColor(rowColors);

    // 화살표를 눌렀다면 목록을 다시 세울 근거가 생긴 것이다. 검색 중에는 걸린
    // 것을 전부 펼쳐 두므로(옛 동작) 누름을 적지 않는다 — 검색을 끈 뒤에 엉뚱한
    // 가지가 접혀 있는 일이 없도록.
    if (!m_searchFilter.IsActive() && ImGui::IsItemToggledOpen())
        m_flat.Toggle(row.index);

    auto* draw = ImGui::GetWindowDrawList();
    const auto* window = ImGui::GetCurrentWindow();
    const bool locked = EditorObjectOperations::IsEditLocked(obj);
    const float textRight = window->WorkRect.Max.x - (locked ? iconSize + editor::ThemePixels(4.f) : 0.f);
    draw->PushClipRect(window->WorkRect.Min, ImVec2(textRight, window->WorkRect.Max.y), true);
    if (auto* image = EditorAssetPresentation::Get().GetEntityIcon(obj->m_editorIcon))
        draw->AddImage(EditorImGuiTexture::From(image), imageMin,
            ImVec2(imageMin.x+iconSize,imageMin.y+iconSize), ImVec2(0,0), ImVec2(1,1),
            IM_COL32(255,255,255,isDisabled ? 100 : 255));
    draw->AddText(ImVec2(imageMin.x+iconSize+editor::ThemePixels(4.f), imageMin.y),
        ImGui::GetColorU32(ImGuiCol_Text), obj->m_name.ToString().c_str());
    draw->PopClipRect();
    if (locked)
        draw->AddText(ImVec2(textRight, imageMin.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), EditorIcon::Lock);
    ImGui::PopStyleColor();

	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
	{
		if (ImGui::IsItemHovered() && (ImGui::IsMouseReleased(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
		{
			bool shift = InputManagement->IsKeyPressed((int)KeyBoard::LeftShift);
            auto newList = selectedObjects;
            const auto found = std::find(newList.begin(), newList.end(), obj);
            if (shift) { if (found != newList.end()) newList.erase(found); else newList.push_back(obj); }
            else newList = {obj};
            std::vector<EntityHandle> targets;
            for (auto* object : newList) if (object) targets.push_back(scene->HandleOf(object->m_index));
            EditorObjectOperations::Select(scene, targets);
		}
	}

	if (!EditorObjectOperations::IsEditLocked(obj, true) && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT", &obj->m_index, sizeof(Entity::Index));
		ImGui::Text("Moving %s", obj->m_name.ToString().c_str());
		ImGui::EndDragDropSource();
	}

	if (!EditorObjectOperations::IsEditLocked(obj) && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
			// 부모 변경 로직
			if (draggedIndex != obj->m_index) // 자기 자신에 드롭하는 것 방지
			{
				const auto& draggedObj = scene->GetEntity(draggedIndex);
				// E1 후속 배선: 위 드롭 타겟(씬 루트)과 동일한 사유 — 페이로드 인덱스가
				// 이미 파괴된 슬롯을 가리키면 draggedObj/oldParent가 nullptr일 수 있다.
				if (draggedObj)
				{
					EditorObjectOperations::Parent(scene->HandleOf(draggedObj->m_index), scene->HandleOf(obj->m_index));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

}

// PHASE 21 W3: 생성자 안 람다였던 본문. 옮긴 것은 들여쓰기뿐이다.
void HierarchyWindow::Draw()
{
			const editor::HierarchyStyleScope hierarchyStyle;
			// 홀짝 띠는 창 한 판 안에서만 뜻이 있다 — 매 프레임 0부터 센다.
			m_rowIndex = 0;

			Scene* scene = SceneManagers->GetActiveScene();
			RenderScene* renderScene = SceneManagers->GetRenderScene();
			Entity* selectedSceneObject = nullptr;
			static bool isSceneObjectSelected = false;
			const bool sceneReady = !SceneManagers->IsSceneLoading() && scene && renderScene;

			ImGui::BeginDisabled(!sceneReady);
			ImGui::PushStyleColor(ImGuiCol_Text, hierarchy_color(editor::HierarchyThemeTokens::EntityText));
			// A square icon button cannot fit the normal 7px horizontal padding
			// on both sides. Keep its height and center the full icon line box.
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, ImGui::GetStyle().FramePadding.y));
			if (ImGui::Button(EditorIcon::Label<EditorIcon::Add, "##HierarchyCreate">,
				ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
				ImGui::OpenPopup("HierarchyCreateMenu");
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create object");
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			if (ImGui::InputTextWithHint("##HierarchyWindow Search",
				EditorIcon::Label<EditorIcon::Search, "  Search">,
				m_searchFilter.InputBuf, IM_COUNTOF(m_searchFilter.InputBuf)))
				m_searchFilter.Build();

			if (ImGui::BeginPopup("HierarchyCreateMenu"))
			{
				if (sceneReady) draw_creation_menu(scene);
				ImGui::EndPopup();
			}
			if (!sceneReady)
			{
				ImGui::TextDisabled(SceneManagers->IsSceneLoading() ? "Loading scene..." : "No active scene");
				return;
			}

			// Only the tree scrolls. Search/create stay outside the list and retain focus.
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
			const bool treeVisible = ImGui::BeginChild("##HierarchyTree", ImVec2(0.f, 0.f),
				ImGuiChildFlags_Borders);
			ImGui::PopStyleVar();
			if (!treeVisible)
			{
				ImGui::EndChild();
				return;
			}
			if (scene && renderScene)
			{
				selectedSceneObject = scene->m_selectedEntity;

				// W8-2: 선택이 바뀌면 그 줄을 화면 안으로 끌어온다. W7-3 이 목록에
				// clipping 을 세운 뒤로 화면 밖 줄은 그려지지 않으므로, 선택이 밖에
				// 있으면 사람은 그것이 어디 있는지 알 길이 없다.
				const int selectionIndex = (nullptr != selectedSceneObject)
					? static_cast<int>(selectedSceneObject->m_index) : -1;
				if (selectionIndex != m_lastSelectionIndex)
				{
					m_lastSelectionIndex = selectionIndex;
					if (selectionIndex >= 0)
					{
						// 펼치는 것이 먼저다. 조상이 접혀 있으면 그 줄은 목록에
						// 없고, 없는 줄로는 스크롤할 수 없다. 목록은 이 뒤에 선다.
						m_flat.ExpandAncestors(scene, selectionIndex);
						m_requestScrollToSelection = true;
					}
				}

				if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput)
				{
					bool ctrl = InputManagement->IsKeyPressed((int)KeyBoard::LeftControl);
					if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
					{
						m_clipboard = scene->m_selectedEntities;
					}
					if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
					{
						scene->ClearSelectedEntities();
						Meta::UndoManager::GetInstance()->Execute(std::make_unique<Meta::DuplicateGameObjectsCommand>(
							scene, std::span<Entity* const>(m_clipboard.data(), m_clipboard.size())));
					}
				}

				if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
				{
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						ImGui::OpenPopup("HierarchyMenu");
					}

					if (false == ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						scene->m_selectedEntity = nullptr;
						scene->m_selectedEntities.clear();
					}
				}

				if (ImGui::BeginPopup("HierarchyMenu"))
				{
					if (ImGui::MenuItem("		Undo", "		Ctrl + Z"))
					{
						EditorObjectOperations::UndoRedo(false);
					}
					if (ImGui::MenuItem("		Redo", "		Ctrl + Y"))
					{
						EditorObjectOperations::UndoRedo(true);
					}
					if (ImGui::MenuItem("       Copy", "       Ctrl + C", nullptr, !scene->m_selectedEntities.empty()))
					{
						m_clipboard = scene->m_selectedEntities;
					}
					if (ImGui::MenuItem("       Paste", "	Ctrl + V", nullptr, !m_clipboard.empty()))
					{
						scene->ClearSelectedEntities();
						Meta::UndoManager::GetInstance()->Execute(std::make_unique<Meta::DuplicateGameObjectsCommand>(
							scene, std::span<Entity* const>(m_clipboard.data(), m_clipboard.size())));
					}
					if (ImGui::MenuItem("		Delete", "		Del", nullptr, isSceneObjectSelected && !EditorObjectOperations::IsEditLocked(selectedSceneObject, true)))
					{
						if (selectedSceneObject)
						{
							if (EditorObjectOperations::Delete(scene->HandleOf(selectedSceneObject->m_index)).IsSuccess())
                                scene->m_selectedEntity = nullptr;
						}
					}
					ImGui::Separator();

					draw_creation_menu(scene);

					// 선언된 Hierarchy 팝업 항목(PHASE 21 M1). 문맥은 선택 엔티티의 신원이다.
					// 항목 0 이면 구분선조차 넣지 않는다(A.6) — 그래서 배선 착지만으로는 이
					// 팝업의 픽셀이 달라지지 않는다.
					if (::editor::popup_host_has_items(::editor::popup_host::hierarchy))
					{
						if (const std::optional<::editor::entity_target> target =
							::editor::targets::selected_entity())
						{
							ImGui::Separator();
							::editor::draw_popup_menu_items<::editor::popup_host::hierarchy>(*target);
						}
					}
					ImGui::EndPopup();
				}

				if (selectedSceneObject && ImGui::IsWindowFocused() &&
					!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
				{
					if (EditorObjectOperations::Delete(scene->HandleOf(selectedSceneObject->m_index)).IsSuccess())
                        scene->m_selectedEntity = nullptr;
				}
			}

			ImVec2 availSize = ImGui::GetContentRegionAvail();
			ImVec2 windowPos = ImGui::GetCursorScreenPos();
			ImRect dropRect(windowPos, ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y));

			if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("MyDropTarget")))
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Model"))
				{
					const file::path filepath = editor::asset_drag::path_of(*payload);

					if (scene)
					{
	                    Editor::ModelPlacement::Get().Execute(scene->GetSceneId(), filepath.string());
					}
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_TEXTURE"))
				{
					//TODO : 불필요 로직 제거 -> DataSystem에서 LoadUITexture로 변경
					const file::path filepath = editor::asset_drag::path_of(*payload);
					const file::path filename = filepath.filename();
					auto texture = DataSystems->LoadSharedTexture(filepath.string().c_str(), DataSystem::TextureFileType::UITexture);
					ImageComponent* sprite = nullptr;
					if (selectedSceneObject)
					{
						if (ImageComponent* hasSprite = selectedSceneObject->GetComponent<ImageComponent>())
						{
							sprite = hasSprite;
						}
						else
						{
							sprite = selectedSceneObject->AddComponent<ImageComponent>();
						}

						if (sprite)
						{
							sprite->Load(texture);
						}
					}
					else
					{
						ImGui::Text("No Entity Selected");
						UIManagers->MakeImage(filename.stem().string().c_str(), texture);
					}
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
				{
					const file::path filepath = editor::asset_drag::path_of(*payload);
					const file::path filename = filepath.filename();
					auto texture = DataSystems->LoadSharedTexture(filepath.string().c_str(), DataSystem::TextureFileType::Texture);

					if (scene)
					{
						auto obj = scene->CreateEntity(filename.stem().string().c_str(), GameObjectType::Empty);
						if (obj)
						{
							auto spriteRenderer = obj->AddComponent<SpriteRenderer>();
							if (spriteRenderer)
							{
								spriteRenderer->SetSprite(texture);
							}
						}
					}
				}
				//else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Prefab"))
				//{
				//	const char* droppedFilePath = (const char*)payload->Data;
				//	file::path filename = droppedFilePath;
				//	file::path filepath = PathFinder::Relative("Prefabs\\") / filename.filename();
				//	if (scene)
				//	{
				//		Meta::UndoManager::GetInstance()->Execute(
				//			std::make_unique<Meta::LoadPrefabToSceneObjCommand>(
				//				scene,
				//				DataSystems->LoadCashedPrefab(filepath.string())));
				//	}
				//}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Font"))
				{
					const file::path filepath = editor::asset_drag::path_of(*payload);
					const file::path filename = filepath.filename();
					if (selectedSceneObject)
					{
						TextComponent* text = nullptr;
						if (TextComponent* hasText = selectedSceneObject->GetComponent<TextComponent>())
						{
							text = hasText;
						}
						else
						{
							text = selectedSceneObject->AddComponent<TextComponent>();
						}
						if (text)
						{
							text->SetFont(filepath);
							text->SetMessage("New Text");
						}
					}
					else
					{
						ImGui::Text("No Entity Selected");
						UIManagers->MakeText(filename.stem().string().c_str(), filepath);
					}
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITESHEET"))
				{
					const file::path filepath = editor::asset_drag::path_of(*payload);
					const file::path filename = filepath.filename();
					if (!editor::asset_drag::lives_in(filepath, "SpriteSheets", "Hierarchy sprite sheet drop"))
					{
						// 거부 사유는 lives_in 이 로그에 남겼다 — 스프라이트 시트는 이름만 저장한다.
					}
					else if (selectedSceneObject)
					{
						SpriteSheetComponent* spriteSheet = nullptr;
						if (SpriteSheetComponent* hasSpriteSheet = selectedSceneObject->GetComponent<SpriteSheetComponent>())
						{
							spriteSheet = hasSpriteSheet;
						}
						else
						{
							spriteSheet = selectedSceneObject->AddComponent<SpriteSheetComponent>();
						}
						if (spriteSheet)
						{
							spriteSheet->LoadSpriteSheet(filepath.string());
						}
					}
					else
					{
						ImGui::Text("No Entity Selected");
						UIManagers->MakeSpriteSheet(filename.stem().string().c_str(), filepath.string());
					}
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
				{
					Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
					// 부모 변경 로직
					if (draggedIndex != 0) // 자기 자신에 드롭하는 것 방지
					{
						Entity* sceneGameObject = scene->GetEntity(0);
						const auto& draggedObj = scene->GetEntity(draggedIndex);
						// E1(슬롯맵)의 GetEntity 루트 폴백 제거 후속 배선: 드래그 페이로드의
						// 인덱스가 이미 파괴된 슬롯을 가리킬 수 있다(예전엔 루트로 조용히
						// 대체됐다) — sceneGameObject/draggedObj/oldParent 중 하나라도 없으면
						// 역참조 없이 포기한다.
						if (sceneGameObject && draggedObj)
						{
							EditorObjectOperations::Parent(scene->HandleOf(draggedObj->m_index), scene->HandleOf(sceneGameObject->m_index));
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (scene && renderScene)
			{
				// 줄과 줄은 맞닿는다 — 한 줄의 높이는 kRowFlags의 FramePadding이
				// 세우고, 줄 사이의 빈틈은 여기서 0으로 지운다.
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
					ImVec2(editor::ThemePixels(editor::EditorThemeTokens::ItemGapX), 0.f));

				// W7-2: 루트를 두 번 훑고 재귀로 내려가던 자리다. 이제 목록은
				// 한 번 세워 두고, 계층이 그대로면 다시 세우지 않는다. 무효화
				// 근거를 여기서 모아 넘긴다 — 무엇이 근거인지 한자리에서 보이도록.
				const editor::hierarchy_flat_key flatKey{
					scene->GetHierarchyStore().Revision(),
					scene->m_Entities.size(),
					SceneManagers->GetDontDestroyOnLoadObjects().size(),
					m_searchFilter.IsActive() };
				const auto& flatRows = m_flat.Rows(scene, flatKey, m_searchFilter);

				if (flatRows.empty())
				{
					ImGui::Text("No Entity in Scene");
				}
				else
				{
					// 들여쓰기는 TreePush 대신 여기서 준다. 줄마다 독립이라
					// 어느 줄부터 그려도 같은 그림이 나온다 — clipping 의 전제다.
					const float indentStep = ImGui::GetStyle().IndentSpacing;

					// W7-3: 화면 밖의 줄은 그리지 않는다. 한 줄의 높이는 kRowFlags 의
					// FramePadding 이 세운 GetFrameHeight() 이고, 바로 위에서 ItemSpacing.y 를
					// 0 으로 눌렀으므로 줄 간격이 곧 그 높이다 — clipper 에게 그 값을 그대로
					// 준다. 목록이 평탄하지 않았다면 인덱스로 끊을 수 없으니 이 자리 자체가
					// 없었다(W7-2).
					const float rowHeight = ImGui::GetFrameHeight();
					ImGuiListClipper rowClipper;
					rowClipper.Begin(static_cast<int>(flatRows.size()), rowHeight);

					// W8-2: clipper 는 화면 밖 줄을 아예 밟지 않는다. 끌어올 줄을
					// 따로 **청구**하지 않으면 그 줄의 자리를 알 길이 없다 —
					// `IncludeItemByIndex` 가 그 자리이고 `Begin` 뒤가 그 때다.
					//
					// ★ 이 블록은 줄 높이 선언과 `ImGuiListClipper` **사이에 두지
					//   않는다.** 그 인접성으로 "clipper 에 준 줄 높이" 를 찾는
					//   소스 대조 게이트가 있다(verify-hierarchy-flatten-contract).
					int revealRow = -1;
					if (m_requestScrollToSelection && m_lastSelectionIndex >= 0)
					{
						for (std::size_t at = 0; at < flatRows.size(); ++at)
						{
							if (editor::hierarchy_row_kind::entity != flatRows[at].kind) continue;
							if (flatRows[at].index != m_lastSelectionIndex) continue;
							revealRow = static_cast<int>(at);
							break;
						}
						// 목록에 없다(검색이 걸러냈다). 다음 프레임에도 같은 목록이니
						// 매달리지 않고 내린다.
						if (revealRow < 0) m_requestScrollToSelection = false;
					}
					if (revealRow >= 0) rowClipper.IncludeItemByIndex(revealRow);

					while (rowClipper.Step())
					{
						for (int rowAt = rowClipper.DisplayStart; rowAt < rowClipper.DisplayEnd; ++rowAt)
						{
							const auto& flatRow = flatRows[static_cast<std::size_t>(rowAt)];

							// W8-2: 끌어올 줄이면 **지금 커서가 선 자리**가 그 줄의
							// 자리다. 보폭을 여기서 다시 계산하지 않는다 — 계산하면
							// clipper 의 보폭과 두 벌이 되고, 어긋나도 조용하다.
							// 이미 온전히 보이면 흔들지 않는다.
							if (rowAt == revealRow)
							{
								const float rowTop = ImGui::GetCursorPosY();
								const float viewTop = ImGui::GetScrollY();
								const float viewHeight = ImGui::GetWindowHeight();
								if (rowTop < viewTop || rowTop + rowHeight > viewTop + viewHeight)
									ImGui::SetScrollY(rowTop + (rowHeight - viewHeight) * 0.5f);
								m_requestScrollToSelection = false;
							}

							const float indent = indentStep * static_cast<float>(flatRow.depth);
							if (indent > 0.f) ImGui::Indent(indent);

							switch (flatRow.kind)
							{
							case editor::hierarchy_row_kind::scene_group:
							{
								const std::string sceneName = scene->m_Entities[0]->m_name.ToString();
								const std::string sceneIcon = EditorIcon::Scene + std::string(" ") + sceneName;
								ImGui::SetNextItemOpen(true, ImGuiCond_Always);
								group_tree_node(sceneIcon.c_str());
								if (sceneName == "PrefabEditor" &&
									ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
									ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
								{
									// PrefabEditor가 열려있다면 닫기
									if (PrefabEditors->IsOpened())
									{
										PrefabEditors->Close();
									}
								}
								break;
							}
							case editor::hierarchy_row_kind::ddol_group:
								ImGui::SetNextItemOpen(true, ImGuiCond_Always);
								group_tree_node("[ Dont Destroy On Load ]");
								break;
							case editor::hierarchy_row_kind::entity:
								// E1 후속 배선: 무효 인덱스는 nullptr 을 돌려줄 수 있다 —
								// 한 줄 그리기는 obj 를 무가드로 역참조하므로 여기서 거른다.
								if (Entity* rowEntity = scene->GetEntity(flatRow.index))
								{
									ImGui::PushID(flatRow.index);
									DrawSceneObjectRow(rowEntity, flatRow);
									ImGui::PopID();
								}
								else
								{
									// clipper 는 한 줄을 건너뛴 것을 모른다 — 자리를 비워
									// 두지 않으면 아래 줄이 통째로 밀린다.
									ImGui::Dummy(ImVec2(0.f, rowHeight));
								}
								break;
							}

							if (indent > 0.f) ImGui::Unindent(indent);
						}
					}
					rowClipper.End();
				}

				ImGui::PopStyleVar();
			}

			// W7-0: m_rowIndex 는 DrawSceneObject 가 그린 행의 수다(띠 색을 매기는
			// 그 수). clipping 이 서면 이 값이 "보이는 행" 으로 줄어야 한다.
			editor::windows::add_panel_units(editor::windows::panel_cost_slot::hierarchy,
				static_cast<std::uint64_t>(m_rowIndex < 0 ? 0 : m_rowIndex));
			isSceneObjectSelected = nullptr != selectedSceneObject ? true : false;
			ImGui::EndChild();
}
