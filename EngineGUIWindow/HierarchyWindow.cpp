#include "HierarchyWindow.h"
#include "SpriteRenderer.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "GameObject.h"
#include "Model.h"
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
			GameObject* selectedSceneObject = nullptr;
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
				selectedSceneObject = scene->m_selectedSceneObject;

				if (ImGui::IsWindowFocused())
				{
					bool ctrl = InputManagement->IsKeyPressed((int)KeyBoard::LeftControl);
					if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C))
					{
						m_clipboard = scene->m_selectedSceneObjects;
					}
					if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V))
					{
						scene->ClearSelectedSceneObjects();
						Meta::UndoCommandManager->Execute(std::make_unique<Meta::DuplicateGameObjectsCommand>(
							scene, std::span<GameObject* const>(m_clipboard.data(), m_clipboard.size())));
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
						scene->m_selectedSceneObject = nullptr;
						scene->m_selectedSceneObjects.clear();
					}
				}

				ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
				if (ImGui::BeginPopup("HierarchyMenu"))
				{
					if (ImGui::MenuItem("		Undo", "		Ctrl + Z"))
					{
						Meta::UndoCommandManager->Undo();
					}
					if (ImGui::MenuItem("		Redo", "		Ctrl + Y"))
					{
						Meta::UndoCommandManager->Redo();
					}
					if (ImGui::MenuItem("       Copy", "       Ctrl + C", nullptr, !scene->m_selectedSceneObjects.empty()))
					{
						m_clipboard = scene->m_selectedSceneObjects;
					}
					if (ImGui::MenuItem("       Paste", "	Ctrl + V", nullptr, !m_clipboard.empty()))
					{
						scene->ClearSelectedSceneObjects();
						Meta::UndoCommandManager->Execute(std::make_unique<Meta::DuplicateGameObjectsCommand>(
							scene, std::span<GameObject* const>(m_clipboard.data(), m_clipboard.size())));
					}
					if (ImGui::MenuItem("		Delete", "		Del", nullptr, isSceneObjectSelected))
					{
						if (selectedSceneObject)
						{
							Meta::UndoCommandManager->Execute(std::make_unique<Meta::DeleteGameObjectCommand>(scene, selectedSceneObject->m_index));
							scene->m_selectedSceneObject = nullptr;
						}
					}
					ImGui::Separator();

					if (ImGui::MenuItem("		Create Empty", "		Ctrl + Shift + N"))
					{
						Meta::UndoCommandManager->Execute(std::make_unique<Meta::CreateGameObjectCommand>(scene, "GameObject", GameObjectType::Empty));
					}

					if (ImGui::BeginMenu("		Light"))
					{
						if (ImGui::MenuItem("		Directional Light"))
						{
							auto obj = scene->CreateGameObject("Directional Light", GameObjectType::Light);
							auto comp = obj->AddComponent<LightComponent>();
							comp->m_lightType = LightType::DirectionalLight;
							comp->m_lightStatus = LightStatus::Enabled;
						}
						if (ImGui::MenuItem("		Point Light"))
						{
							auto obj = scene->CreateGameObject("Point Light", GameObjectType::Light);
							auto comp = obj->AddComponent<LightComponent>();
							comp->m_lightType = LightType::PointLight;
							comp->m_lightStatus = LightStatus::Enabled;
						}
						if (ImGui::MenuItem("		Spot Light"))
						{
							auto obj = scene->CreateGameObject("Spot Light", GameObjectType::Light);
							obj->Transform_().SetRotation({ 0.7, 0, 0, 1 });
							auto comp = obj->AddComponent<LightComponent>();
							comp->m_lightType = LightType::SpotLight;
							comp->m_lightStatus = LightStatus::Enabled;
						}
						ImGui::EndMenu();
					}

					if (ImGui::MenuItem("		Camera"))
					{
						auto obj = scene->CreateGameObject("Camera", GameObjectType::Camera);
						auto comp = obj->AddComponent<CameraComponent>();
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
					Meta::UndoCommandManager->Execute(std::make_unique<Meta::DeleteGameObjectCommand>(scene, selectedSceneObject->m_index));
					scene->m_selectedSceneObject = nullptr;
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
						Meta::UndoCommandManager->Execute(
							std::make_unique<Meta::LoadModelToSceneObjCommand>(
								scene,
								DataSystems->LoadCashedModel(filepath.string())));
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
						ImGui::Text("No GameObject Selected");
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
						auto obj = scene->CreateGameObject(filename.stem().string().c_str(), GameObjectType::Empty);
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
				//		Meta::UndoCommandManager->Execute(
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
						ImGui::Text("No GameObject Selected");
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
						ImGui::Text("No GameObject Selected");
						UIManagers->MakeSpriteSheet(filename.stem().string().c_str(), filepath.string());
					}
				}
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
				{
					GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
					// 부모 변경 로직
					if (draggedIndex != 0) // 자기 자신에 드롭하는 것 방지
					{
						GameObject* sceneGameObject = scene->GetGameObject(0).get();
						const auto& draggedObj = scene->GetGameObject(draggedIndex);
						// E1(슬롯맵)의 GetGameObject 루트 폴백 제거 후속 배선: 드래그 페이로드의
						// 인덱스가 이미 파괴된 슬롯을 가리킬 수 있다(예전엔 루트로 조용히
						// 대체됐다) — sceneGameObject/draggedObj/oldParent 중 하나라도 없으면
						// 역참조 없이 포기한다.
						if (sceneGameObject && draggedObj)
						{
							// 최상위 오브젝트는 m_parentIndex가 INVALID_INDEX이고 씬 루트의 children에
							// 매달려 있다(씬 전체가 쓰는 규약) — 그때 떼어낼 상대는 씬 루트다.
							// 폴백이 없으면 GetGameObject(-1)이 널이라 아래 블록이 통째로 건너뛰어,
							// 부모 없이 만든 오브젝트(카메라·라이트 등)는 드래그해도 아무 일도
							// 일어나지 않는다.
							auto oldParent = scene->GetGameObject(draggedObj->m_parentIndex);
							if (!oldParent) oldParent = scene->GetGameObject(GameObject::kSceneRootIndex);
							if (oldParent)
							{
								// 1. 기존 부모에서 제거
								oldParent->DetachChildIndex(draggedIndex);

								// 2. 새로운 부모에 추가
								// 최상위 규약은 INVALID_INDEX다. 0(씬 루트 인덱스)을 넣으면
								// 로더가 루트 children을 재구성할 때 IsInvalidIndex인 것만
								// 다시 붙이므로(SceneManager), 저장 후 다시 열면 이 오브젝트가
								// 루트 목록에서 빠져 계층에서 사라진다.
								draggedObj->SetParentIndex(GameObject::INVALID_INDEX);
								sceneGameObject->AttachChildIndex(draggedIndex);
								if (auto* rect = draggedObj->GetComponent<RectTransformComponent>())
								{
									rect->SetParentKeepWorldPosition(sceneGameObject);
								}
							}
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (scene && renderScene)
			{
				std::string SceneIcon{};
				if (0 != scene->m_SceneObjects.size())
				{
					SceneIcon = ICON_FA_BOLT + std::string(" ") + scene->m_SceneObjects[0]->m_name.ToString();
				}
				ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				if (0 == scene->m_SceneObjects.size())
				{
					ImGui::Text("No GameObject in Scene");
				}
				else if (ImGui::TreeNodeEx(SceneIcon.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					bool isPrefabEditor = scene->m_SceneObjects[0]->m_name.ToString() == "PrefabEditor";
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

					auto& sceneObjects = scene->m_SceneObjects;
					for (int i = 1; i < sceneObjects.size(); ++i)
					{
						auto& obj = sceneObjects[i];
						if (!obj || obj->m_parentIndex > 0 || obj->IsDontDestroyOnLoad()) continue;

						ImGui::PushID((int)&obj);
						DrawSceneObject(obj);
						ImGui::PopID();
					}

					std::vector<std::shared_ptr<GameObject>> ddolObjects;
					for (int i = 1; i < sceneObjects.size(); ++i)
					{
						auto& obj = sceneObjects[i];
						if (obj && obj->IsDontDestroyOnLoad())
						{
							ddolObjects.push_back(obj);
						}
					}

					if (!ddolObjects.empty())
					{
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);
						if (ImGui::TreeNodeEx("[ Dont Destroy On Load ]", ImGuiTreeNodeFlags_DefaultOpen))
						{
							for (const auto& obj : ddolObjects)
							{
								ImGui::PushID((int)obj.get());
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

void HierarchyWindow::DrawSceneObject(const std::shared_ptr<GameObject>& obj)
{
	auto scene = SceneManagers->GetActiveScene();
	auto& selectedSceneObject = scene->m_selectedSceneObject;
	auto& selectedObjects = scene->m_selectedSceneObjects;

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
	bool isSelected = std::find(selectedObjects.begin(), selectedObjects.end(), obj.get()) != selectedObjects.end() || scene->m_selectedSceneObject == obj.get();
	if (isSelected)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	else if (0 == obj->m_parentIndex)
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	if (0 == obj->m_childrenIndices.size())
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
			std::vector<GameObject*> prevList = selectedObjects;
			GameObject* prevSelection = selectedSceneObject;

			if (shift)
			{
				if (std::find(selectedObjects.begin(), selectedObjects.end(), obj.get()) != selectedObjects.end())
					scene->RemoveSelectedSceneObject(obj.get());
				else
					scene->AddSelectedSceneObject(obj.get());
			}
			else
			{
				scene->ClearSelectedSceneObjects();
				scene->AddSelectedSceneObject(obj.get());
			}

			auto newList = scene->m_selectedSceneObjects;
			GameObject* newSelection = scene->m_selectedSceneObject;

			Meta::MakeCustomChangeCommand(
				[scene, prevList, prevSelection]() {
					scene->m_selectedSceneObjects = prevList;
					scene->m_selectedSceneObject = prevSelection;
				},
				[scene, newList, newSelection]() {
					scene->m_selectedSceneObjects = newList;
					scene->m_selectedSceneObject = newSelection;
				}
			);
		}
	}

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT", &obj->m_index, sizeof(GameObject::Index));
		ImGui::Text("Moving %s", obj->m_name.ToString().c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
			// 부모 변경 로직
			if (draggedIndex != obj->m_index) // 자기 자신에 드롭하는 것 방지
			{
				const auto& draggedObj = scene->GetGameObject(draggedIndex);
				// E1 후속 배선: 위 드롭 타겟(씬 루트)과 동일한 사유 — 페이로드 인덱스가
				// 이미 파괴된 슬롯을 가리키면 draggedObj/oldParent가 nullptr일 수 있다.
				if (draggedObj)
				{
					// 최상위 오브젝트는 m_parentIndex가 INVALID_INDEX이고 씬 루트의 children에
					// 매달려 있다(씬 전체가 쓰는 규약) — 그때 떼어낼 상대는 씬 루트다.
					// 폴백이 없으면 GetGameObject(-1)이 널이라 아래 블록이 통째로 건너뛰어,
					// 부모 없이 만든 오브젝트(카메라·라이트 등)는 드래그해도 아무 일도
					// 일어나지 않는다.
					auto oldParent = scene->GetGameObject(draggedObj->m_parentIndex);
					if (!oldParent) oldParent = scene->GetGameObject(GameObject::kSceneRootIndex);
					if (oldParent)
					{
						// 1. 기존 부모에서 제거
						oldParent->DetachChildIndex(draggedIndex);

						// 2. 새로운 부모에 추가
						draggedObj->SetParentIndex(obj->m_index);

						obj->AttachChildIndex(draggedIndex);

						if (auto* rect = draggedObj->GetComponent<RectTransformComponent>())
						{
							rect->SetParentKeepWorldPosition(obj.get());
						}
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (opened)
	{
		// 자식 노드를 재귀적으로 그리기
		for (auto childIndex : obj->m_childrenIndices)
		{
			auto child = scene->GetGameObject(childIndex);
			// E1 후속 배선: 루트 폴백 제거로 무효 인덱스가 nullptr을 돌려줄 수
			// 있다 — DrawSceneObject는 obj를 무가드로 역참조하므로 여기서 거른다.
			if (!child) continue;
			DrawSceneObject(child);
		}
		ImGui::TreePop();
	}
}

bool HierarchyWindow::IsMatchedRecursive(const std::shared_ptr<GameObject>& obj)
{
	if (!obj) return false;

	auto scene = SceneManagers->GetActiveScene();
	if (!scene) return false;

	// 1) 자기 자신 이름으로 필터 체크
	const std::string name = obj->m_name.ToString();
	if (m_searchFilter.PassFilter(name.c_str()))
		return true;

	// 2) 자식들 재귀 체크
	for (auto childIndex : obj->m_childrenIndices)
	{
		auto child = scene->GetGameObject(childIndex);
		if (child && IsMatchedRecursive(child))
			return true;
	}

	return false;
}

