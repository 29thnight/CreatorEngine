#include "HierarchyWindow.h"
#include "SceneRenderer.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "GameObject.h"
#include "Model.h"
#include "LightComponent.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "DataSystem.h"
#include "PathFinder.h"

HierarchyWindow::HierarchyWindow(SceneRenderer* ptr) :
	m_sceneRenderer(ptr)
{
	ImGui::ContextRegister("Hierarchy", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		Scene* scene = nullptr;
		RenderScene* renderScene = nullptr;
		GameObject* selectedSceneObject = nullptr;
		static bool isSceneObjectSelected = false;

		if (m_sceneRenderer)
		{
			scene = SceneManagers->GetActiveScene();
			renderScene = m_sceneRenderer->m_renderScene;
			selectedSceneObject = renderScene->m_selectedSceneObject;

			if (!scene && !renderScene)
			{
				ImGui::Text("Not Init HierarchyWindow");
				ImGui::End();
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
					renderScene->m_selectedSceneObject = nullptr;
				}
			}

			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
			if (ImGui::BeginPopup("HierarchyMenu"))
			{
				if (ImGui::MenuItem("		Delete", "		Del", nullptr, isSceneObjectSelected))
				{
					if (selectedSceneObject)
					{
						scene->DestroyGameObject(selectedSceneObject->m_index);
						renderScene->m_selectedSceneObject = nullptr;
					}
				}
				ImGui::Separator();

				if (ImGui::MenuItem("		Create Empty", "		Ctrl + Shift + N"))
				{
					scene->CreateGameObject("GameObject", GameObject::Type::Empty);
				}

				if (ImGui::BeginMenu("		Light"))
				{
					if (ImGui::MenuItem("		Directional Light"))
					{
						auto obj = scene->CreateGameObject("Directional Light", GameObject::Type::Light);
						auto comp = obj->AddComponent<LightComponent>();
						comp->m_lightType = LightType::DirectionalLight;
						comp->m_lightStatus = LightStatus::Enabled;
					}
					if (ImGui::MenuItem("		Point Light")) 
					{
						auto obj = scene->CreateGameObject("Point Light", GameObject::Type::Light);
						auto comp = obj->AddComponent<LightComponent>();
						comp->m_lightType = LightType::PointLight;
						comp->m_lightStatus = LightStatus::Enabled;
					}
					if (ImGui::MenuItem("		Spot Light"))
					{
						auto obj = scene->CreateGameObject("Spot Light", GameObject::Type::Light);
						//obj->m_transform.SetRotation({});
						auto comp = obj->AddComponent<LightComponent>();
						comp->m_lightType = LightType::SpotLight;
						comp->m_lightStatus = LightStatus::Enabled;
					}
					ImGui::EndMenu();
				}

				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);

			if (selectedSceneObject && ImGui::IsKeyDown(ImGuiKey_Delete))
			{
				scene->DestroyGameObject(selectedSceneObject->m_index);
				renderScene->m_selectedSceneObject = nullptr;
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

				if(scene)
				{
					Model::LoadModelToScene(DataSystems->LoadCashedModel(filepath.string().c_str()), *scene);
				}
			}
			else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
			{
				const char* droppedFilePath = (const char*)payload->Data;
				file::path filename = droppedFilePath;
				file::path filepath = PathFinder::Relative("UI\\") / filename.filename();
				Texture* texture = DataSystems->LoadTexture(filepath.string().c_str());
				ImageComponent* sprite = nullptr;
				if (selectedSceneObject)
				{
					if (ImageComponent* hasSprite = selectedSceneObject->GetComponent<ImageComponent>())
						sprite = hasSprite;
					else
						sprite = selectedSceneObject->AddComponent<ImageComponent>();
					if (sprite)
					{
						sprite->Load(texture);
					}
				}
				else
				{
					ImGui::Text("No GameObject Selected");
					UIManagers->MakeImage(filename.string().c_str(), texture);
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
					const auto& oldParent = scene->GetGameObject(draggedObj->m_parentIndex);

					// 1. 기존 부모에서 제거
					auto& siblings = oldParent->m_childrenIndices;
					std::erase_if(siblings, [&](auto index) { return index == draggedIndex; });

					// 2. 새로운 부모에 추가
					draggedObj->m_parentIndex = 0;
					sceneGameObject->m_childrenIndices.push_back(draggedIndex);
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (m_sceneRenderer)
		{
			if (0 == scene->m_SceneObjects.size())
			{
				ImGui::Text("No GameObject in Scene");
			}
			else if (ImGui::TreeNodeEx(scene->m_SceneObjects[0]->m_name.ToString().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (auto& obj : scene->m_SceneObjects)
				{
					if (0 == obj->m_index || obj->m_parentIndex > 0) continue;

					ImGui::PushID((int)&obj);
					DrawSceneObject(obj);
					ImGui::PopID();
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
	auto& selectedSceneObject = m_sceneRenderer->m_renderScene->m_selectedSceneObject;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
	if (obj.get() == selectedSceneObject)
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

	bool opened = ImGui::TreeNodeEx(obj->m_name.ToString().c_str(), flags);

	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
	{
		if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
		{
			GameObject* prevSelection = selectedSceneObject; // 선택되기 전 값
			GameObject* newSelection = obj.get();            // 선택될 값

			if (prevSelection != newSelection)
			{
				Meta::MakeCustomChangeCommand(
					[=]() { m_sceneRenderer->m_renderScene->m_selectedSceneObject = prevSelection; },
					[=]() { m_sceneRenderer->m_renderScene->m_selectedSceneObject = newSelection; }
				);

				// 즉시 반영
				selectedSceneObject = newSelection;
			}
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
				const auto& oldParent = scene->GetGameObject(draggedObj->m_parentIndex);

				// 1. 기존 부모에서 제거
				auto& siblings = oldParent->m_childrenIndices;
				std::erase_if(siblings, [&](auto index) { return index == draggedIndex; });

				// 2. 새로운 부모에 추가
				draggedObj->m_parentIndex = obj->m_index;
				obj->m_childrenIndices.push_back(draggedIndex);
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
			DrawSceneObject(child);
		}
		ImGui::TreePop();
	}
}
