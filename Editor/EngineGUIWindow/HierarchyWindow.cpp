#include "EditorObjectOperations.h"
#include "HierarchyWindow.h"
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
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "InputManager.h"
#include "MetaStateCommand.h"
#include "ReflectionRegister.h"

HierarchyWindow::HierarchyWindow()
{
	ImGui::ContextRegister(ICON_FA_BARS_STAGGERED "  Hierarchy", [&]()
		{
			ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

			Scene* scene = nullptr;
			RenderScene* renderScene = nullptr;
			Entity* selectedSceneObject = nullptr;
			static bool isSceneObjectSelected = false;

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
			ImGui::BeginDisabled();
			ImGui::Button(ICON_FA_MAGNIFYING_GLASS " Search");
			ImGui::EndDisabled();
			ImGui::SameLine();
			m_searchFilter.Draw("##HierarchyWindow Search", ImGui::GetContentRegionAvail().x);
			ImGui::PopStyleVar();

			if (SceneManagers->IsSceneLoading())
			{
				ImGui::Text("Not Init HierarchyWindow");
				//ImGui::End();
				return;
			}

			scene = SceneManagers->GetActiveScene();
			renderScene = SceneManagers->GetRenderScene();
			if (scene && renderScene)
			{
				selectedSceneObject = scene->m_selectedEntity;

				if (ImGui::IsWindowFocused())
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

				if (!scene && !renderScene)
				{
					ImGui::Text("Not Init HierarchyWindow");
					//ImGui::End();
					return;
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

				ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
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
					if (ImGui::MenuItem("		Delete", "		Del", nullptr, isSceneObjectSelected))
					{
						if (selectedSceneObject)
						{
							EditorObjectOperations::Delete(scene->HandleOf(selectedSceneObject->m_index));
							scene->m_selectedEntity = nullptr;
						}
					}
					ImGui::Separator();

					if (ImGui::MenuItem("		Create Empty", "		Ctrl + Shift + N"))
					{
						EditorObjectOperations::Create(scene, "Entity", GameObjectType::Empty);
					}

					if (ImGui::BeginMenu("		Light"))
					{
						if (ImGui::MenuItem("		Directional Light"))
						{
							auto creation = EditorObjectOperations::Create(scene, "Directional Light", GameObjectType::Light);
                            auto* obj = creation.IsSuccess() ? scene->TryGetEntity(static_cast<Entity::Index>(creation.data.Find("index")->AsInt())) : nullptr;
							auto comp = obj ? obj->GetComponent<LightComponent>() : nullptr;
                            if (comp) { comp->SetLightType(LightType::DirectionalLight);
							comp->m_lightStatus = LightStatus::Enabled; }
						}
						if (ImGui::MenuItem("		Point Light"))
						{
							auto creation = EditorObjectOperations::Create(scene, "Point Light", GameObjectType::Light);
                            auto* obj = creation.IsSuccess() ? scene->TryGetEntity(static_cast<Entity::Index>(creation.data.Find("index")->AsInt())) : nullptr;
							auto comp = obj ? obj->GetComponent<LightComponent>() : nullptr;
                            if (comp) { comp->SetLightType(LightType::PointLight);
							comp->m_lightStatus = LightStatus::Enabled; }
						}
						if (ImGui::MenuItem("		Spot Light"))
						{
							auto creation = EditorObjectOperations::Create(scene, "Spot Light", GameObjectType::Light);
                            auto* obj = creation.IsSuccess() ? scene->TryGetEntity(static_cast<Entity::Index>(creation.data.Find("index")->AsInt())) : nullptr;
	if (obj) obj->Transform_().SetRotation(
		{ 0.7, 0, 0, 1 }, TransformWriteReason::Inspector);
							auto comp = obj ? obj->GetComponent<LightComponent>() : nullptr;
                            if (comp) { comp->SetLightType(LightType::SpotLight);
							comp->m_lightStatus = LightStatus::Enabled; }
						}
						ImGui::EndMenu();
					}

					if (ImGui::MenuItem("		Camera"))
					{
						EditorObjectOperations::Create(scene, "Camera", GameObjectType::Camera);
					}

					//TODO : 아직 처리가 안된듯
					if (ImGui::BeginMenu("		UI"))
					{
						if (ImGui::MenuItem("		Image"))
						{
							UIManagers->MakeImage("NoneImage", nullptr);
						}
						if (ImGui::MenuItem("		Text"))
						{
							UIManagers->MakeText("Text", "null", nullptr);
						}
						if (ImGui::MenuItem("		Button"))
						{

						}
						ImGui::EndMenu();
					}
					ImGui::EndPopup();
				}
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);

				if (selectedSceneObject && ImGui::IsKeyDown(ImGuiKey_Delete))
				{
					EditorObjectOperations::Delete(scene->HandleOf(selectedSceneObject->m_index));
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
					const char* droppedFilePath = (const char*)payload->Data;
					file::path filename = droppedFilePath;
					file::path filepath = PathFinder::Relative("Models\\") / filename.filename();

					if (scene)
					{
						Meta::UndoManager::GetInstance()->Execute(
							std::make_unique<Meta::LoadModelToSceneObjCommand>(
								scene,
								DataSystems->LoadModelAssetGenerationByPath(filepath.string())));
					}
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_TEXTURE"))
				{
					//TODO : 불필요 로직 제거 -> DataSystem에서 LoadUITexture로 변경
					const char* droppedFilePath = (const char*)payload->Data;
					file::path filename = droppedFilePath;
					file::path filepath = PathFinder::Relative("UI\\") / filename.filename();
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
					const char* droppedFilePath = (const char*)payload->Data;
					file::path filename = droppedFilePath;
					file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
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
					const char* droppedFilePath = (const char*)payload->Data;
					file::path filename = droppedFilePath;
					file::path filepath = PathFinder::Relative("Font\\") / filename.filename();
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
					const char* droppedFilePath = (const char*)payload->Data;
					file::path filename = droppedFilePath;
					file::path filepath = PathFinder::Relative("SpriteSheets\\") / filename.filename();
					if (selectedSceneObject)
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
				std::string SceneIcon{};
				if (0 != scene->m_Entities.size())
				{
					SceneIcon = ICON_FA_BOLT + std::string(" ") + scene->m_Entities[0]->m_name.ToString();
				}
				ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				if (0 == scene->m_Entities.size())
				{
					ImGui::Text("No Entity in Scene");
				}
				else if (ImGui::TreeNodeEx(SceneIcon.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					bool isPrefabEditor = scene->m_Entities[0]->m_name.ToString() == "PrefabEditor";
					if (isPrefabEditor &&
						ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						// PrefabEditor가 열려있다면 닫기
						if (PrefabEditors->IsOpened())
						{
							PrefabEditors->Close();
						}
					}

					auto& sceneObjects = scene->m_Entities;
					for (int i = 1; i < sceneObjects.size(); ++i)
					{
						auto& obj = sceneObjects[i];
						if (!obj || obj->GetParentIndex() > 0 || obj->IsDontDestroyOnLoad()) continue;

						ImGui::PushID(obj.get());
						DrawSceneObject(obj.get());
						ImGui::PopID();
					}

					std::vector<Entity*> ddolObjects;
					for (int i = 1; i < sceneObjects.size(); ++i)
					{
						auto& obj = sceneObjects[i];
						if (obj && obj->IsDontDestroyOnLoad())
						{
							ddolObjects.push_back(obj.get());
						}
					}

					if (!ddolObjects.empty())
					{
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);
						if (ImGui::TreeNodeEx("[ Dont Destroy On Load ]", ImGuiTreeNodeFlags_DefaultOpen))
						{
							for (const auto& obj : ddolObjects)
							{
								ImGui::PushID(obj);
								DrawSceneObject(obj);
								ImGui::PopID();
							}
							ImGui::TreePop();
						}
					}

					ImGui::TreePop();
				}
			}

			isSceneObjectSelected = nullptr != selectedSceneObject ? true : false;

		}, ImGuiWindowFlags_NoMove);
}

void HierarchyWindow::DrawSceneObject(Entity* obj)
{
	auto scene = SceneManagers->GetActiveScene();
	auto& selectedSceneObject = scene->m_selectedEntity;
	auto& selectedObjects = scene->m_selectedEntities;

	// 🔍 검색 필터가 활성화된 경우, 자기 자신 + 자식들까지 재귀 검사
	if (m_searchFilter.IsActive())
	{
		// 자식에 검색 결과가 있는 경우까지 보여주고 싶다면
		// 여기서 바로 return하지 말고,
		// "자식 중 하나라도 필터를 통과하면 이 노드도 그려준다"
		// 같은 재귀 체크 로직이 더 필요.
		if (!IsMatchedRecursive(obj))
		{
			// 자기 자신과 모든 자식이 필터에 안 걸리면 아예 그리지 않음
			return;
		}

		// 검색 중에는 매치되는 애들은 기본적으로 열어두면 편함
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
	bool isSelected = std::find(selectedObjects.begin(), selectedObjects.end(), obj) != selectedObjects.end() || scene->m_selectedEntity == obj;
	if (isSelected)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	else if (0 == obj->GetParentIndex())
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	if (obj->GetChildrenIndices().empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

	if (!obj->IsEnabled())
	{
		// 회색으로 텍스트 색상 변경
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
	}

	std::string icon{};

	if (obj->m_prefab)
	{
		icon = ICON_FA_BOX_OPEN + std::string(" ") + obj->m_name.ToString();
	}
	else
	{
		icon = ICON_FA_CUBE + std::string(" ") + obj->m_name.ToString();
	}
	bool opened = ImGui::TreeNodeEx(icon.c_str(), flags);


	if (!obj->IsEnabled())
	{
		ImGui::PopStyleColor();
	}

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

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT", &obj->m_index, sizeof(Entity::Index));
		ImGui::Text("Moving %s", obj->m_name.ToString().c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
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

	if (opened)
	{
		// 자식 노드를 재귀적으로 그리기
		for (auto childIndex : obj->GetChildrenIndices())
		{
			auto child = scene->GetEntity(childIndex);
			// E1 후속 배선: 루트 폴백 제거로 무효 인덱스가 nullptr을 돌려줄 수
			// 있다 — DrawSceneObject는 obj를 무가드로 역참조하므로 여기서 거른다.
			if (!child) continue;
			DrawSceneObject(child);
		}
		ImGui::TreePop();
	}
}

bool HierarchyWindow::IsMatchedRecursive(Entity* obj)
{
	if (!obj) return false;

	auto scene = SceneManagers->GetActiveScene();
	if (!scene) return false;

	// 1) 자기 자신 이름으로 필터 체크
	const std::string name = obj->m_name.ToString();
	if (m_searchFilter.PassFilter(name.c_str()))
		return true;

	// 2) 자식들 재귀 체크
	for (auto childIndex : obj->GetChildrenIndices())
	{
		auto child = scene->GetEntity(childIndex);
		if (child && IsMatchedRecursive(child))
			return true;
	}

	return false;
}

