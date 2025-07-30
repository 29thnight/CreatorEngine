#ifndef DYNAMICCPP_EXPORTS
#include "InspectorWindow.h"
#include "SceneRenderer.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "GameObject.h"
#include "Model.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "DataSystem.h"
#include "PathFinder.h"
#include "Transform.h"
#include "ComponentFactory.h"
#include "ReflectionImGuiHelper.h"
#include "CustomCollapsingHeader.h"
#include "Terrain.h"
#include "FileDialog.h"
#include "TagManager.h"
#include "PlayerInput.h"
#include "InputActionManager.h"
//----------------------------
#include "NodeFactory.h"
#include "ExternUI.h"
#include "StateMachineComponent.h"
#include "BehaviorTreeComponent.h"
#include "FoliageComponent.h"
#include "FunctionRegistry.h"
//----------------------------

#include "IconsFontAwesome6.h"
#include "fa.h"
#include "PinHelper.h"
#include "NodeEditor.h"
#include <algorithm>

namespace ed = ax::NodeEditor;

ed::EditorContext* m_fsmEditorContext{ nullptr };
bool			   s_CreatingLink = false;
ed::PinId		   s_LinkStartPin = 0;
ed::LinkId		   s_EditLinkId = 0;
bool			   s_RenameNodePopup{ false };

ed::EditorContext* s_BTEditorContext{ nullptr };

constexpr XMVECTOR FORWARD = XMVECTOR{ 0.f, 0.f, 1.f, 0.f };
constexpr XMVECTOR UP = XMVECTOR{ 0.f, 1.f, 0.f, 0.f };

InspectorWindow::InspectorWindow(SceneRenderer* ptr) :
	m_sceneRenderer(ptr)
{
	ImGui::ContextRegister(ICON_FA_CIRCLE_INFO "  Inspector", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		static GameObject* prevSelectedSceneObject = nullptr;
		static bool wasMetaSelectedLastFrame = false;

		Scene* scene = nullptr;
		RenderScene* renderScene = nullptr;
		GameObject* selectedSceneObject = nullptr;
		std::optional<MetaYml::Node>& selectedNode{ DataSystems->selectedFileMetaNode };
		bool isSelectedNode = selectedNode.has_value();
		file::path selectedFileName{ DataSystems->selectedFileName };
		file::path selectedMetaFilePath{ DataSystems->selectedMetaFilePath };

		if (m_sceneRenderer)
		{
			scene = SceneManagers->GetActiveScene();
			renderScene = m_sceneRenderer->m_renderScene.get();
			selectedSceneObject = scene->m_selectedSceneObject;

			if (!scene && !renderScene)
			{
				ImGui::Text("Not Init InspectorWindow");
				ImGui::End();
				return;
			}
		}

		bool sceneObjectJustSelected = (selectedSceneObject != nullptr && selectedSceneObject != prevSelectedSceneObject);

		bool metaNodeJustSelected = (isSelectedNode && !wasMetaSelectedLastFrame);

		// 3. 우선순위 결정
		if (sceneObjectJustSelected)
		{
			// 게임 오브젝트 선택 시 YAML 선택 해제
			selectedNode = std::nullopt;
			isSelectedNode = false;
			wasMetaSelectedLastFrame = false;
		}

		if (metaNodeJustSelected)
		{
			// 메타 파일 선택 시 게임 오브젝트 해제
			selectedSceneObject = nullptr;
			prevSelectedSceneObject = nullptr;
			wasMetaSelectedLastFrame = true;
		}

		if (scene && selectedSceneObject)
		{
			std::string name = selectedSceneObject->m_name.ToString();
			ImGui::Checkbox("##Enabled", &selectedSceneObject->m_isEnabled);
			ImGui::SameLine();

			selectedSceneObject->SetEnabled(selectedSceneObject->m_isEnabled);

			if (ImGui::InputText("##name",
				&name[0],
				name.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
				Meta::InputTextCallback,
				static_cast<void*>(&name)))
			{
				selectedSceneObject->m_name.SetString(name);
			}

			ImGui::SameLine();
			ImGui::Checkbox("Static", &selectedSceneObject->m_isStatic);
			
			auto& tag_manager = TagManager::GetInstance();
			auto& tags = tag_manager->GetTags();
			auto& layers = tag_manager->GetLayers();
			int tagCount = static_cast<int>(tags.size());
			int layerCount = static_cast<int>(layers.size());
			static int prevTagCount = 0;
			static int prevLayerCount = 0;
			static int selectedTagIndex = 0;
			static int selectedLayerIndex = 0;

			static const char* tagNames[64]{};
			if(0 == prevTagCount || tagCount != prevTagCount)
			{
				memset(tagNames, 0, sizeof(tagNames));
				for (int i = 0; i < tagCount; ++i) {
					tagNames[i] = tags[i].c_str(); // Assuming TagManager::GetTags() returns a vector of strings
				}
			}

			static const char* layerNames[64]{};
			if(0 == prevLayerCount || layerCount != prevLayerCount)
			{
				memset(layerNames, 0, sizeof(layerNames));
				for (int i = 0; i < layerCount; ++i) {
					layerNames[i] = layers[i].c_str(); // Assuming TagManager::GetLayers() returns a vector of strings
				}
			}

			auto& selectedTag = selectedSceneObject->m_tag;
			auto& selectedLayer = selectedSceneObject->m_layer;

			selectedTagIndex = tag_manager->GetTagIndex(selectedTag.ToString());
			selectedLayerIndex = tag_manager->GetLayerIndex(selectedLayer.ToString());
			if (selectedTagIndex < 0 || selectedTagIndex >= tagCount)
			{
				selectedTagIndex = 0; // 기본값으로 첫 번째 태그 선택
			}
			// Tag 콤보박스
			ImGui::Text("Tag");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f); // 픽셀 단위로 너비 설정
			if (ImGui::BeginCombo("##TagCombo", tagNames[selectedTagIndex]))
			{
				for (int i = 0; i <= tagCount; ++i)
				{
					bool isSelected = false;
					if (i == tagCount) // "Add Tag" 항목
					{
						if (ImGui::Selectable("Add Tag"))
						{
							m_openNewTagPopup = true; // 팝업 열기 플래그 설정
						}
					}
					else
					{
						isSelected = (selectedTag == tagNames[i]);
						if (ImGui::Selectable(tagNames[i], isSelected))
						{
							tag_manager->RemoveTagFromObject(selectedTag.ToString(), selectedSceneObject);
							selectedTag = tagNames[i];
							tag_manager->AddTagToObject(selectedTag.ToString(), selectedSceneObject);
							selectedTagIndex = i; // 선택된 인덱스 업데이트
						}
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::Text("Layer");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f); // 픽셀 단위로 너비 설정
			if (ImGui::BeginCombo("##LayerCombo", layerNames[selectedTagIndex]))
			{
				for (int i = 0; i < layerCount; ++i)
				{
					bool isSelected = false;
					if (i == layerCount - 1) // "Add Layer" 항목
					{
						if (ImGui::Selectable("Add Layer"))
						{
							m_openNewLayerPopup = true; // 팝업 열기 플래그 설정
						}
					}
					else
					{
						isSelected = (selectedLayer == layerNames[i]);
						if (ImGui::Selectable(layerNames[i], isSelected))
						{
							tag_manager->RemoveObjectFromLayer(selectedLayer.ToString(), selectedSceneObject);
							selectedLayer = layerNames[i];
							tag_manager->AddObjectToLayer(selectedLayer.ToString(), selectedSceneObject);
							selectedLayerIndex = i; // 선택된 인덱스 업데이트
						}
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			
			prevTagCount = tagCount;
			prevLayerCount = layerCount;

			if (m_openNewTagPopup)
			{
				ImGui::OpenPopup("New Tag");
				m_openNewTagPopup = false; // 팝업 열기 플래그 초기화
			}

			if (m_openNewLayerPopup)
			{
				ImGui::OpenPopup("New Layer");
				m_openNewLayerPopup = false; // 팝업 열기 플래그 초기화
			}

			// New Tag 팝업
			if (ImGui::BeginPopup("New Tag"))
			{
				static char newTagName[64] = "";
				ImGui::InputText("Tag Name", newTagName, sizeof(newTagName));
				if (ImGui::Button("Add"))
				{
					if (strlen(newTagName) > 0)
					{
						tag_manager->AddTag(newTagName);
						selectedTag = newTagName;
						selectedTagIndex = tagCount; // 새로 추가된 태그 인덱스
						tagCount = tag_manager->GetTags().size(); // 태그 개수 업데이트
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			// New Layer 팝업
			if (ImGui::BeginPopup("New Layer"))
			{
				static char newLayerName[64] = "";
				ImGui::InputText("Layer Name", newLayerName, sizeof(newLayerName));
				if (ImGui::Button("Add"))
				{
					if (strlen(newLayerName) > 0)
					{
						tag_manager->AddLayer(newLayerName);
						selectedLayer = newLayerName;
						selectedLayerIndex = layerCount; // 새로 추가된 레이어 인덱스
						layerCount = tag_manager->GetLayers().size(); // 레이어 개수 업데이트
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			// 현재 트랜스폼 값
			Mathf::Vector4& position = selectedSceneObject->m_transform.position;
			Mathf::Vector4& rotation = selectedSceneObject->m_transform.rotation;
			Mathf::Vector4& scale = selectedSceneObject->m_transform.scale;

			// ===== POSITION =====
			static bool editingPosition = false;
			static Mathf::Vector4 prevPosition{};

			float pyr[3]; // pitch yaw roll
			Mathf::QuaternionToEular(rotation, pyr[0], pyr[1], pyr[2]);

			for (float& i : pyr)
			{
				i *= Mathf::Rad2Deg;
			}

			bool menuClicked = false;
			if (ImGui::DrawCollapsingHeaderWithButton("Transform", ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_BARS, &menuClicked))
			{
				ImGui::Text("Position ");
				ImGui::SameLine();
				if (ImGui::DragFloat3("##Position", &position.x, 0.08f, -1000, 1000))
				{
					if (!editingPosition)
					{
						prevPosition = position;
						editingPosition = true;
					}
					selectedSceneObject->m_transform.m_dirty = true;
				}
				if (editingPosition && ImGui::IsItemDeactivatedAfterEdit())
				{
					if (prevPosition != position)
					{
						Meta::MakeCustomChangeCommand([=]
						{
							selectedSceneObject->m_transform.position = prevPosition;
							selectedSceneObject->m_transform.m_dirty = true;
						},
						[=]
						{
							selectedSceneObject->m_transform.position = position;
							selectedSceneObject->m_transform.m_dirty = true;
						});
					}
					editingPosition = false;
				}

				static bool editingRotation = false;
				static Mathf::Vector4 prevRotation{};
				static float prevEuler[3] = {};

				float pyr[3];
				float deltaEuler[3] = { 0, 0, 0 };
				Mathf::QuaternionToEular(rotation, pyr[0], pyr[1], pyr[2]);

				float prevPYR[3];
				prevRotation = rotation;

				for (float& i : pyr) i *= Mathf::Rad2Deg;
				prevPYR[0] = pyr[0];
				prevPYR[1] = pyr[1];
				prevPYR[2] = pyr[2];

				ImGui::Text("Rotation ");
				ImGui::SameLine();
				if (ImGui::DragFloat3("##Rotation", pyr, 0.1f))
				{
					if (!editingRotation)
					{
						prevRotation = rotation;
						prevEuler[0] = pyr[0];
						prevEuler[1] = pyr[1];
						prevEuler[2] = pyr[2];
						editingRotation = true;
					}
					Mathf::Vector3 radianEuler(
						pyr[0] - prevPYR[0],
						pyr[1] - prevPYR[1],
						pyr[2] - prevPYR[2]);
					rotation = XMQuaternionMultiply(XMQuaternionRotationRollPitchYaw(radianEuler.x * Mathf::Deg2Rad, radianEuler.y * Mathf::Deg2Rad, radianEuler.z * Mathf::Deg2Rad), rotation);
					selectedSceneObject->m_transform.m_dirty = true;
				}
				if (editingRotation && ImGui::IsItemDeactivatedAfterEdit())
				{
					Mathf::Vector3 prevEulerRad(prevEuler[0] * Mathf::Deg2Rad, prevEuler[1] * Mathf::Deg2Rad, prevEuler[2] * Mathf::Deg2Rad);
					Mathf::Vector4 compare = XMQuaternionRotationRollPitchYaw(prevEulerRad.x, prevEulerRad.y, prevEulerRad.z);
					if (compare != rotation)
					{
						Meta::MakeCustomChangeCommand([=]
						{
							selectedSceneObject->m_transform.rotation = prevRotation;
							selectedSceneObject->m_transform.m_dirty = true;
						},
						[=]
						{
							selectedSceneObject->m_transform.rotation = rotation;
							selectedSceneObject->m_transform.m_dirty = true;
						});
					}
					editingRotation = false;
				}

				static bool editingScale = false;
				static Mathf::Vector4 prevScale{};

				ImGui::Text("Scale     ");
				ImGui::SameLine();
				if (ImGui::DragFloat3("##Scale", &scale.x, 0.1f, 0.001f, 1000.f))
				{
					if (!editingScale)
					{
						prevScale = scale;
						editingScale = true;
					}
					selectedSceneObject->m_transform.m_dirty = true;
				}
				if (editingScale && ImGui::IsItemDeactivatedAfterEdit())
				{
					if (prevScale != scale)
					{
						Meta::MakeCustomChangeCommand([=]
						{
							selectedSceneObject->m_transform.scale = prevScale;
							selectedSceneObject->m_transform.m_dirty = true;
						},
						[=]
						{
							selectedSceneObject->m_transform.scale = scale;
							selectedSceneObject->m_transform.m_dirty = true;
						});
					}
					editingScale = false;
				}

				{
					selectedSceneObject->m_transform.UpdateLocalMatrix();
				}
			}

			static bool isOpen = false;
			static Component* selectedComponent = nullptr;

			for (auto& component : selectedSceneObject->m_components)
			{
				if(nullptr == component)
					continue;

				const auto& type = Meta::Find(component->ToString());
				ModuleBehavior* moduleBehavior{ dynamic_cast<ModuleBehavior*>(component.get()) };
				std::string componentBaseName = component->ToString();
				if (!type && !moduleBehavior) continue;

				if (nullptr != moduleBehavior)
				{
					componentBaseName += " (Script)";
				}

				bool* isEnabled = &component->m_isEnabled;
				if (ImGui::DrawCollapsingHeaderWithButton(componentBaseName.c_str(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_BARS, &isOpen, isEnabled))
				{
					if(isOpen && nullptr == selectedComponent)
					{
						selectedComponent = component.get();
					}

					if(component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>())
					{
						MeshRenderer* meshRenderer = dynamic_cast<MeshRenderer*>(component.get());
						if (nullptr != meshRenderer)
						{
							ImGuiDrawHelperMeshRenderer(meshRenderer);
						}
					}
					else if (component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<TerrainComponent>()) {

						TerrainComponent* terrain = dynamic_cast<TerrainComponent*>(component.get());
						if (nullptr != terrain)
						{
							ImGuiDrawHelperTerrainComponent(terrain);
						}

					}
					else if (nullptr != moduleBehavior)
					{
						ImGuiDrawHelperModuleBehavior(moduleBehavior);
					}
					else if (component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<Animator>())
					{
						Animator* animator = dynamic_cast<Animator*> (component.get());
						if (nullptr != animator)
						{
							ImGuiDrawHelperAnimator(animator);
						}
					}
					else if (component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<StateMachineComponent>())
					{
						StateMachineComponent* fsm = dynamic_cast<StateMachineComponent*>(component.get());
						if (nullptr != fsm)
						{
							ImGuiDrawHelperFSM(fsm);
						}
					}
					else if (component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<BehaviorTreeComponent>())
					{
						BehaviorTreeComponent* bt = dynamic_cast<BehaviorTreeComponent*>(component.get());
						if (nullptr != bt)
						{
							ImGuiDrawHelperBT(bt);
						}
					}
					else if (component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<PlayerInputComponent>())
					{
						PlayerInputComponent* input = dynamic_cast<PlayerInputComponent*>(component.get());
						if (nullptr != input)
						{
							ImGuiDrawHelperPlayerInput(input);
						}
					}
					else if (type)
					{
						Meta::DrawObject(component.get(), *type);
					}
				}
			}
			
			ImGui::Separator();
			ImVec2 windowSize = ImGui::GetWindowSize();      // 현재 윈도우의 전체 크기
			ImVec2 buttonSize = ImVec2(180, 0);              // 버튼 가로 크기 (세로는 자동 계산됨)

			static ImGuiTextFilter searchFilter;

			ImGui::SetCursorPosX((windowSize.x - buttonSize.x) * 0.5f);  // 수평 중앙 정렬

			if (ImGui::Button("Add Component", buttonSize))
			{
				ImGui::OpenPopup("AddComponent");
			}

			ImGui::SetNextWindowSize(ImVec2(windowSize.x, 0)); // 원하는 사이즈 지정
			if (ImGui::BeginPopup("AddComponent"))
			{
				ImGui::TextColored(ImVec4(1, 1, 0, 1), "Add Component"); // 노란색 텍스트
				ImGui::Separator(); // 구분선

				float availableWidth = ImGui::GetContentRegionAvail().x;
				searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);

				for (const auto& [type_name, type] : ComponentFactorys->m_componentTypes)
				{
					if (!searchFilter.PassFilter(type_name.c_str()))
						continue;

					if (type_name.empty())
					{
						const_cast<std::string&>(type_name) = "None";
					}

					if (ImGui::MenuItem(type_name.c_str()))
					{
						auto component = selectedSceneObject->AddComponent(*type);
					}
				}

				if (ImGui::MenuItem("Scripts"))
				{
					m_openScriptPopup = true;
				}

				if (ImGui::MenuItem("new Script"))
				{
					m_openNewScriptPopup = true;
				}

				ImGui::EndPopup();
			}

			// 다음 프레임에서 열기
			if (m_openScriptPopup)
			{
				ImGui::OpenPopup("Scripts");
				m_openScriptPopup = false;
			}

			ImGui::SetNextWindowSize(ImVec2(windowSize.x, 0)); // 원하는 사이즈 지정
			if (ImGui::BeginPopup("Scripts"))
			{
				float availableWidth = ImGui::GetContentRegionAvail().x;
				searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);
				for (const auto& type_name : ScriptManager->GetScriptNames())
				{
					if (!searchFilter.PassFilter(type_name.c_str()))
						continue;
					if (type_name.empty())
					{
						const_cast<std::string&>(type_name) = "None";
					}

					if (ImGui::MenuItem(type_name.c_str()))
					{
						selectedSceneObject->AddScriptComponent(type_name);
					}
				}
				ImGui::EndPopup();
			}

			// 다음 프레임에서 열기
			if (m_openNewScriptPopup)
			{
				ImGui::OpenPopup("NewScript");
				m_openNewScriptPopup = false;
			}

			if (isOpen)
			{
				ImGui::OpenPopup("ComponentMenu");
				isOpen = false;
			}
			if (menuClicked) {
				ImGui::OpenPopup("TransformMenu");
				menuClicked = false;
			}

			ImGui::SetNextWindowSize(ImVec2(windowSize.x, 0)); // 원하는 사이즈 지정
			if (ImGui::BeginPopup("NewScript"))
			{
				float availableWidth = ImGui::GetContentRegionAvail().x;
				searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);
				static char scriptName[64] = "NewBehaviourScript";
				ImGui::InputText("Name", scriptName, sizeof(scriptName));
				std::string scriptNameStr(scriptName);
				auto scriptBodyFilePath = PathFinder::Relative("Script\\" + scriptNameStr + ".h");
				bool isDisabled = false;
				if (file::exists(scriptBodyFilePath))
				{
					ImGui::Text("Script already exists.");
					isDisabled = true;
				}
				else if (scriptNameStr.empty())
				{
					ImGui::Text("Script name cannot be empty.");
					isDisabled = true;
				}
				else if (scriptNameStr.find_first_of("0123456789") == 0)
				{
					ImGui::Text("Script name cannot start with a number.");
					isDisabled = true;
				}
				else if (scriptNameStr.find_first_of("!@#$%^&*()_+[]{}|;':\",.<>?`~") != std::string::npos)
				{
					ImGui::Text("Script name contains invalid characters.");
					isDisabled = true;
				}

				ImGui::BeginDisabled(isDisabled);
				if (ImGui::Button("Create and Add"))
				{

					if (!scriptNameStr.empty())
					{
						ScriptManager->CreateScriptFile(scriptNameStr);
						ScriptManager->SetCompileEventInvoked(true);
						ScriptManager->ReloadDynamicLibrary();
						selectedSceneObject->AddScriptComponent(scriptNameStr);
						scriptNameStr.clear();
					}
					else
					{
						Debug->LogError("Script name cannot be empty.");
					}
				}
				ImGui::EndDisabled();

				ImGui::EndPopup();
			}

			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
			if (ImGui::BeginPopup("ComponentMenu"))
			{
				if (ImGui::MenuItem("		Remove Component"))
				{
					if (selectedComponent) {
						selectedSceneObject->RemoveComponent(selectedComponent);
					}
					ImGui::CloseCurrentPopup();
					selectedComponent = nullptr;
				}
				ImGui::EndPopup();
			}
			if (ImGui::BeginPopup("TransformMenu")) {
				if (ImGui::MenuItem("Reset Transform"))
				{
					selectedSceneObject->m_transform.position = { 0, 0, 0, 1 };
					selectedSceneObject->m_transform.rotation = XMQuaternionIdentity();
					selectedSceneObject->m_transform.scale = { 1, 1, 1, 1 };
					selectedSceneObject->m_transform.m_dirty = true;
					selectedSceneObject->m_transform.UpdateLocalMatrix();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
		}
		else if (isSelectedNode)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
			
			std::string stem = selectedFileName.stem().string();

			stem += " Import Settings";

			if (ImGui::CollapsingHeader(stem.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawYamlNodeEditor(*selectedNode);

				ImGui::Spacing();
				if (ImGui::Button("Save"))
				{
					try
					{
						YAML::Emitter emitter;
						emitter << *selectedNode;

						std::ofstream fout(selectedMetaFilePath);
						if (fout.is_open())
						{
							fout << emitter.c_str();
							fout.close();
						}
						else
						{
							Debug->LogError("Failed to open file for writing: " + selectedMetaFilePath.string());
						}
					}
					catch (const std::exception& e)
					{
						Debug->LogError("Failed to save YAML: " + std::string(e.what()));
					}
				}
			}
			ImGui::PopStyleVar(2);
		}

		prevSelectedSceneObject = selectedSceneObject;
		wasMetaSelectedLastFrame = isSelectedNode;

	}, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
}

void InspectorWindow::ImGuiDrawHelperTerrainComponent(TerrainComponent* terrainComponent)
{
	TerrainBrush* g_CurrentBrush = terrainComponent->GetCurrentBrush();

	if (!g_CurrentBrush) return;

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
	
	if (ImGui::InputInt("Width", &editWidth)) {
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if(editWidth < 2)
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

	g_CurrentBrush->m_isEditMode = false;
	// ImGui UI 예시 (에디터 툴 패널 내부)
	if (ImGui::CollapsingHeader("Terrain Brush"))
	{
		//inspector가 화면에 뜨는 경우 브러시가 활성화된 상태로 설정
		g_CurrentBrush->m_isEditMode = true; // 브러시가 활성화된 상태로 설정

		// 모드 선택
		const char* modes[] = { "Raise", "Lower", "Flatten", "PaintLayer", "PaintFoliage", "EraseFoliage" };
		int currentMode = static_cast<int>(g_CurrentBrush->m_mode);
		if (ImGui::Combo("Mode", &currentMode, modes, IM_ARRAYSIZE(modes)))
			g_CurrentBrush->m_mode = static_cast<TerrainBrush::Mode>(currentMode);

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

		// PaintLayer 옵션일 때만 레이어 선택
		if (g_CurrentBrush->m_mode == TerrainBrush::Mode::PaintLayer)
		{
			// 에디터가 가지고 있는 레이어 리스트에서 ID와 이름을 보여줌
			static int selectedLayerIndex = 0;
			std::vector<const char*> layerNames;

			layerNames = terrainComponent->GetLayerNames();


			if (ImGui::Combo("Layer ID", &selectedLayerIndex, layerNames.data(), (int)layerNames.size()))
			{
				g_CurrentBrush->m_layerID = selectedLayerIndex;
			}

			TerrainLayer* layer = terrainComponent->GetLayerDesc((uint32_t)selectedLayerIndex);
			if (layer != nullptr) {
				float tiling = layer->tilling;
				float tempTiling = tiling;
				ImGui::DragFloat("tiling", &tempTiling, 1.0f, 0.01, 4096.0f);
				if (tempTiling!= tiling)
				{
					layer->tilling = tempTiling;
					terrainComponent->UpdateLayerDesc(selectedLayerIndex);
				}
			}

			if (ImGui::Button("AddLayer")) {

				file::path difuseFile = ShowOpenFileDialog(L"");
				std::wstring difuseFileName = difuseFile.filename();
				if (!difuseFile.empty())
				{
					terrainComponent->AddLayer(difuseFile, difuseFileName, 10.0f);
				}
			}
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

			//if (ImGui::Combo("Mask", &selectedMaskIndex, maskNames.data(), (int)maskNames.size()))
			//{
			//	if (selectedMaskIndex == 0) {
			//		uint32_t id = 0xFFFFFFFF; // "None" 선택 시 -1로 설정
			//		g_CurrentBrush->SetMaskID(id); // No mask selected
			//	}
			//	else {
			//		uint32_t id = static_cast<uint32_t>(selectedMaskIndex - 1); // Adjust for "None" option
			//		g_CurrentBrush->SetMaskID(id);
			//	}
			//}
		}


		// 브러시 모양 선택
		if (ImGui::Button("mask texture load")) 
		{
			file::path maskTexture = ShowOpenFileDialog(L"");
			terrainComponent->SetBrushMaskTexture(g_CurrentBrush, maskTexture);
		}

		if (ImGui::CollapsingHeader("Paint Foliage")) 
		{
			g_CurrentBrush->m_isEditMode = true;
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
			ImGui::InputInt("Density", &g_CurrentBrush->m_foliageDensity);
			const char* fModes[] = { "Paint", "Erase" };
			int fm = (g_CurrentBrush->m_mode == TerrainBrush::Mode::EraseFoliage) ? 1 : 0;
			if (ImGui::Combo("Action", &fm, fModes, 2)) { g_CurrentBrush->m_mode = fm == 0 ? TerrainBrush::Mode::PaintFoliage : TerrainBrush::Mode::EraseFoliage; }

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
		}
	}

	//save , load
	if (ImGui::Button("Save Terrain"))
	{
		file::path savePath = ShowSaveFileDialog(L"", L"Save File", PathFinder::Relative("Terrain"));
		if (savePath != L"") {
			std::wstring folderPath = savePath.parent_path().wstring();
			std::wstring fileName = savePath.filename().wstring();
			terrainComponent->Save(folderPath, fileName);
		}
	}

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

void InspectorWindow::ImGuiDrawHelperFSM(StateMachineComponent* FSMComponent)
{
	if (FSMComponent)
	{
		ImGui::Text("State Machine Editor");
		ImGui::Separator();
		if (ImGui::Button("Edit State Machine"))
		{
			m_openFSMPopup = true;
			ImGui::OpenPopup("FSMEditorPopup");
		}
		if (ImGui::BeginPopupModal("FSMEditorPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::Button("Add State"))
			{
				// Add state logic here
			}
			ImGui::EndPopup();
		}
	}
}

void InspectorWindow::ImGuiDrawHelperBT(BehaviorTreeComponent* BTComponent)
{
	if (!BTComponent) return;

	if (ImGui::Button("Set Behavior Tree")) 
	{
		file::path filePath = ShowOpenFileDialog(
			L"Behavior Tree Files (*.bt)\0*.bt\0",
			L"Load Behavior Tree",
			PathFinder::Relative("BehaviorTree").wstring()
		);

		if (!filePath.empty())
		{
			BTComponent->name = filePath.stem().string();
			FileGuid guid = DataSystems->GetFileGuid(filePath);
			if (guid != nullFileGuid)
			{
				BTComponent->m_BehaviorTreeGuid = guid;
			}
			else
			{
				Debug->LogError("Failed to get file GUID for Behavior Tree: " + filePath.string());
			}
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Set BlackBoard"))
	{
		file::path filePath = ShowOpenFileDialog(
			L"BlackBoard Files (*.blackboard)\0*.blackboard\0",
			L"Load BlackBoard",
			PathFinder::Relative("BehaviorTree").wstring()
		);

		if (!filePath.empty())
		{
			BTComponent->blackBoardName = filePath.stem().string();
			FileGuid guid = DataSystems->GetFileGuid(filePath);
			if (guid != nullFileGuid)
			{
				BTComponent->m_BlackBoardGuid = guid;
			}
			else
			{
				Debug->LogError("Failed to get file GUID for Blackboard: " + filePath.string());
			}
		}
	}

	// Behavior Tree 이름 표시
	if (!BTComponent->name.empty())
	{
		ImGui::Text("Behavior Tree: %s", BTComponent->name.c_str());
	}

	//BlackBoard 이름 표시
	if (!BTComponent->blackBoardName.empty())
	{
		ImGui::Text("BlackBoard: %s", BTComponent->blackBoardName.c_str());
	}

}

static std::string s_scriptName;
static std::string s_scriptCode;


// 고정 크기 버퍼 (필요에 따라 크기 조정)
static char nameBuf[128] = "";
static char codeBuf[4096] = "";


void InspectorWindow::ImguiDrawLuaScriptPopup()
{
//	// 열기 버튼
//	if (ImGui::Button("Lua Script Editor"))
//		ImGui::OpenPopup("LuaScriptEditorPopup");
//
//	// 팝업 모달
//	if (ImGui::BeginPopupModal("LuaScriptEditorPopup", nullptr,
//		ImGuiWindowFlags_AlwaysAutoResize))
//	{
//		ImGui::Text("Lua Script Editor");
//		ImGui::Separator();
//
//		// 스크립트 이름 & 코드 입력
//		ImGui::InputText("Function Name", nameBuf, sizeof(nameBuf));
//		ImGui::InputTextMultiline("Script Code", codeBuf, sizeof(codeBuf), ImVec2(-1, 300));
//
//		// -----------------------
//		// Save Script 버튼: 스크립트 본문만 저장
//		// (추후 JSON에 s_scriptCode 함께 직렬화)
//		// -----------------------
//		if (ImGui::Button("Save Script"))
//		{
//			s_scriptName = nameBuf;
//			s_scriptCode = codeBuf;
//			if (LuaEngine::Get().LoadScript(s_scriptCode))
//			{
//				// 성공적으로 로드만 해두고 → 팝업 닫기
//
//				ImGui::CloseCurrentPopup();
//			}
//			else
//			{
//				ImGui::TextColored(ImVec4(1, 0, 0, 1),
//					"Failed to load script. Check console.");
//			}
//		}
//
//		ImGui::Separator();
//
//		// -----------------------
//		// Register as Condition
//		// -----------------------
//		if (ImGui::Button("Register as Condition"))
//		{
//			s_scriptName = nameBuf;
//			s_scriptCode = codeBuf;
//			// 코드가 최신인지 확인
//			if (LuaEngine::Get().LoadScript(s_scriptCode))
//			{
//				ConditionFunc fn = LuaEngine::Get().GetConditionFunction(s_scriptName);
//				if (fn)
//				{
//					FunctionRegistry::RegisterCondition(s_scriptName, fn);
//					ImGui::CloseCurrentPopup();
//				}
//				else
//				{
//					ImGui::TextColored(ImVec4(1, 0, 0, 1),
//						"Function not found: %s", s_scriptName.c_str());
//				}
//			}
//		}
//		ImGui::SameLine();
//
//		// -----------------------
//		// Register as Action
//		// -----------------------
//		if (ImGui::Button("Register as Action"))
//		{
//			s_scriptName = nameBuf;
//			s_scriptCode = codeBuf;
//			if (LuaEngine::Get().LoadScript(s_scriptCode))
//			{
//				auto fn = LuaEngine::Get().GetActionFunction(s_scriptName);
//				if (fn)
//				{
//					FunctionRegistry::RegisterAction(s_scriptName, fn);
//					ImGui::CloseCurrentPopup();
//				}
//				else
//				{
//					ImGui::TextColored(ImVec4(1, 0, 0, 1),
//						"Function not found: %s", s_scriptName.c_str());
//				}
//			}
//		}
//
//		ImGui::SameLine();
//		if (ImGui::Button("Cancel"))
//		{
//			ImGui::CloseCurrentPopup();
//		}
//
//		ImGui::EndPopup();
//	}

}


static float CalcMaxPopupHeightFromItemCount(int items_count)
{
	ImGuiContext& g = *GImGui;
	if (items_count <= 0)
		return FLT_MAX;
	return (g.FontSize + g.Style.ItemSpacing.y) * items_count - g.Style.ItemSpacing.y + (g.Style.WindowPadding.y * 2);
}



bool InspectorWindow::BeginNodeCombo(const char* label, const char* preview_value, ImGuiComboFlags flags)
{
	using namespace ImGui;

	// Always consume the SetNextWindowSizeConstraint() call in our early return paths
	ImGuiContext& g = *GImGui;
	bool has_window_size_constraint = (g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint) != 0;
	g.NextWindowData.Flags &= ~ImGuiNextWindowDataFlags_HasSizeConstraint;

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;

	IM_ASSERT((flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) != (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)); // Can't use both flags together

	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);

	const float arrow_size = (flags & ImGuiComboFlags_NoArrowButton) ? 0.0f : GetFrameHeight();
	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	const float expected_w = CalcItemWidth();
	const float w = (flags & ImGuiComboFlags_NoPreview) ? arrow_size : expected_w;
	const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
	const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id, &frame_bb))
		return false;

	bool hovered, held;
	bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);
	bool popup_open = IsPopupOpen(id, ImGuiPopupFlags_None);

	const ImU32 frame_col = GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
	const float value_x2 = ImMax(frame_bb.Min.x, frame_bb.Max.x - arrow_size);
	RenderNavHighlight(frame_bb, id);
	if (!(flags & ImGuiComboFlags_NoPreview))
		window->DrawList->AddRectFilled(frame_bb.Min, ImVec2(value_x2, frame_bb.Max.y), frame_col, style.FrameRounding, (flags & ImGuiComboFlags_NoArrowButton) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
	if (!(flags & ImGuiComboFlags_NoArrowButton))
	{
		ImU32 bg_col = GetColorU32((popup_open || hovered) ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		ImU32 text_col = GetColorU32(ImGuiCol_Text);
		window->DrawList->AddRectFilled(ImVec2(value_x2, frame_bb.Min.y), frame_bb.Max, bg_col, style.FrameRounding, (w <= arrow_size) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersRight);
		if (value_x2 + arrow_size - style.FramePadding.x <= frame_bb.Max.x)
			RenderArrow(window->DrawList, ImVec2(value_x2 + style.FramePadding.y, frame_bb.Min.y + style.FramePadding.y), text_col, ImGuiDir_Down, 1.0f);
	}
	RenderFrameBorder(frame_bb.Min, frame_bb.Max, style.FrameRounding);
	if (preview_value != NULL && !(flags & ImGuiComboFlags_NoPreview))
		RenderTextClipped(frame_bb.Min + style.FramePadding, ImVec2(value_x2, frame_bb.Max.y), preview_value, NULL, NULL, ImVec2(0.0f, 0.0f));
	if (label_size.x > 0)
		RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

	if ((pressed || g.NavActivateId == id) && !popup_open)
	{
		if (window->DC.NavLayerCurrent == 0)
			window->NavLastIds[0] = id;
		OpenPopupEx(id, ImGuiPopupFlags_None);
		popup_open = true;
	}

	if (!popup_open)
		return false;

	if (has_window_size_constraint)
	{
		g.NextWindowData.Flags |= ImGuiNextWindowDataFlags_HasSizeConstraint;
		g.NextWindowData.SizeConstraintRect.Min.x = ImMax(g.NextWindowData.SizeConstraintRect.Min.x, w);
	}
	else
	{
		if ((flags & ImGuiComboFlags_HeightMask_) == 0)
			flags |= ImGuiComboFlags_HeightRegular;
		IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiComboFlags_HeightMask_));    // Only one
		int popup_max_height_in_items = -1;
		if (flags & ImGuiComboFlags_HeightRegular)     popup_max_height_in_items = 8;
		else if (flags & ImGuiComboFlags_HeightSmall)  popup_max_height_in_items = 4;
		else if (flags & ImGuiComboFlags_HeightLarge)  popup_max_height_in_items = 20;
		SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(FLT_MAX, CalcMaxPopupHeightFromItemCount(popup_max_height_in_items)));
	}

	char name[16];
	ImFormatString(name, IM_ARRAYSIZE(name), "##Combo_%02d", g.BeginPopupStack.Size); // Recycle windows based on depth

	// Position the window given a custom constraint (peak into expected window size so we can position it)
	// This might be easier to express with an hypothetical SetNextWindowPosConstraints() function.
	if (ImGuiWindow* popup_window = FindWindowByName(name))
		if (popup_window->WasActive)
		{
			ImVec2 cursorScreenPos = ed::CanvasToScreen(ImGui::GetCursorScreenPos());
			ed::Suspend();
			ImGui::SetCursorScreenPos(cursorScreenPos);
			// Always override 'AutoPosLastDirection' to not leave a chance for a past value to affect us.
			ImVec2 size_expected = CalcWindowNextAutoFitSize(popup_window);
			if (flags & ImGuiComboFlags_PopupAlignLeft)
				popup_window->AutoPosLastDirection = ImGuiDir_Left; // "Below, Toward Left"
			else
				popup_window->AutoPosLastDirection = ImGuiDir_Down; // "Below, Toward Right (default)"
			ImRect r_outer = GetPopupAllowedExtentRect(popup_window);
			ImVec2 pos = FindBestWindowPosForPopupEx(frame_bb.GetBL(), size_expected, &popup_window->AutoPosLastDirection, r_outer, frame_bb, ImGuiPopupPositionPolicy_ComboBox);
			SetNextWindowPos(pos);
			ed::Resume();
		}

	// We don't use BeginPopupEx() solely because we have a custom name string, which we could make an argument to BeginPopupEx()
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;

	ed::Suspend();

	// Horizontally align ourselves with the framed text
	PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.FramePadding.x, style.WindowPadding.y));
	bool ret = Begin(name, NULL, window_flags);
	PopStyleVar();
	if (!ret)
	{
		EndPopup();
		IM_ASSERT(0);   // This should never happen as we tested for IsPopupOpen() above
		return false;
	}
	return true;
}

void InspectorWindow::EndNodeCombo()
{
	ImGui::EndPopup();

	ed::Resume();
}

#endif // !DYNAMICCPP_EXPORTS
