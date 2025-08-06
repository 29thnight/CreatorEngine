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
#include "VolumeComponent.h"
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
			
			auto& tags = TagManagers->GetTags();
			auto& layers = TagManagers->GetLayers();
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

			selectedTagIndex = TagManagers->GetTagIndex(selectedTag.ToString());
			selectedLayerIndex = TagManagers->GetLayerIndex(selectedLayer.ToString());
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
							TagManagers->RemoveTagFromObject(selectedTag.ToString(), selectedSceneObject);
							selectedTag = tagNames[i];
							TagManagers->AddTagToObject(selectedTag.ToString(), selectedSceneObject);
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
			if (ImGui::BeginCombo("##LayerCombo", layerNames[selectedLayerIndex]))
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
							TagManagers->RemoveObjectFromLayer(selectedLayer.ToString(), selectedSceneObject);
							selectedLayer = layerNames[i];
							selectedSceneObject->SetCollisionType(); // 충돌 타입 업데이트
							TagManagers->AddObjectToLayer(selectedLayer.ToString(), selectedSceneObject);
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
						TagManagers->AddTag(newTagName);
						selectedTag = newTagName;
						selectedTagIndex = tagCount; // 새로 추가된 태그 인덱스
						tagCount = TagManagers->GetTags().size(); // 태그 개수 업데이트
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
						TagManagers->AddLayer(newLayerName);
						selectedLayer = newLayerName;
						selectedLayerIndex = layerCount; // 새로 추가된 레이어 인덱스
						layerCount = TagManagers->GetLayers().size(); // 레이어 개수 업데이트
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
					selectedSceneObject->m_transform.SetDirty();
				}
				if (editingPosition && ImGui::IsItemDeactivatedAfterEdit())
				{
					if (prevPosition != position)
					{
						Meta::MakeCustomChangeCommand([=]
						{
							selectedSceneObject->m_transform.position = prevPosition;
							selectedSceneObject->m_transform.SetDirty();
						},
						[=]
						{
							selectedSceneObject->m_transform.position = position;
							selectedSceneObject->m_transform.SetDirty();
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
					selectedSceneObject->m_transform.SetDirty();
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
							selectedSceneObject->m_transform.SetDirty();
						},
						[=]
						{
							selectedSceneObject->m_transform.rotation = rotation;
							selectedSceneObject->m_transform.SetDirty();
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
					selectedSceneObject->m_transform.SetDirty();
				}
				if (editingScale && ImGui::IsItemDeactivatedAfterEdit())
				{
					if (prevScale != scale)
					{
						Meta::MakeCustomChangeCommand([=]
						{
							selectedSceneObject->m_transform.scale = prevScale;
							selectedSceneObject->m_transform.SetDirty();
						},
						[=]
						{
							selectedSceneObject->m_transform.scale = scale;
							selectedSceneObject->m_transform.SetDirty();
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

					if(component->GetTypeID() == type_guid(MeshRenderer))
					{
						MeshRenderer* meshRenderer = dynamic_cast<MeshRenderer*>(component.get());
						if (nullptr != meshRenderer)
						{
							ImGuiDrawHelperMeshRenderer(meshRenderer);
						}
					}
					else if (component->GetTypeID() == type_guid(TerrainComponent)) {

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
					else if (component->GetTypeID() == type_guid(Animator))
					{
						Animator* animator = dynamic_cast<Animator*> (component.get());
						if (nullptr != animator)
						{
							ImGuiDrawHelperAnimator(animator);
						}
					}
					else if (component->GetTypeID() == type_guid(StateMachineComponent))
					{
						StateMachineComponent* fsm = dynamic_cast<StateMachineComponent*>(component.get());
						if (nullptr != fsm)
						{
							ImGuiDrawHelperFSM(fsm);
						}
					}
					else if (component->GetTypeID() == type_guid(BehaviorTreeComponent))
					{
						BehaviorTreeComponent* bt = dynamic_cast<BehaviorTreeComponent*>(component.get());
						if (nullptr != bt)
						{
							ImGuiDrawHelperBT(bt);
						}
					}
					else if (component->GetTypeID() == type_guid(PlayerInputComponent))
					{
						PlayerInputComponent* input = dynamic_cast<PlayerInputComponent*>(component.get());
						if (nullptr != input)
						{
							ImGuiDrawHelperPlayerInput(input);
						}
					}
					else if (component->GetTypeID() == type_guid(VolumeComponent))
					{
						VolumeComponent* input = dynamic_cast<VolumeComponent*>(component.get());
						if (nullptr != input)
						{
							ImGuiDrawHelperVolume(input);
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
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "Add Component"); // 노란색 텍스트
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
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "ScriptComponent"); // 노란색 텍스트
				ImGui::Separator(); // 구분선

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
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "New ScriptComponent"); // 노란색 텍스트
				ImGui::Separator(); // 구분선

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
					selectedSceneObject->m_transform.SetDirty();
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
	ImGui::SeparatorText("Terrain Size");
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
	if (ImGui::CollapsingHeader("Terrain Editor", ImGuiTreeNodeFlags_DefaultOpen))
	{
		//inspector가 화면에 뜨는 경우 브러시가 활성화된 상태로 설정
		g_CurrentBrush->m_isEditMode = true; // 브러시가 활성화된 상태로 설정
		// 모드 선택
		const char* modes[] = { "Raise", "Lower", "Flatten", "PaintLayer", "FoliageMode" };
		int currentMode = static_cast<int>(g_CurrentBrush->m_mode);
		if (ImGui::Combo("Edit Mode", &currentMode, modes, IM_ARRAYSIZE(modes)))
			g_CurrentBrush->m_mode = static_cast<TerrainBrush::Mode>(currentMode);

		if(g_CurrentBrush->m_mode != TerrainBrush::Mode::FoliageMode)
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
						if (tempTiling != tiling)
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

void InspectorWindow::ImGuiDrawHelperVolume(VolumeComponent* volumeComponent)
{
	if (!volumeComponent) return;

	ImGui::SeparatorText("VolumeProfile");
	ImGui::Text("Drag VolumeProfile Here");
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VolumeProfile"))
		{
			const char* droppedFilePath = static_cast<const char*>(payload->Data);
			file::path filename = file::path(droppedFilePath).filename();
			file::path filepath = PathFinder::Relative("VolumeProfile\\") / filename;
			FileGuid guid = DataSystems->GetFileGuid(filepath);
			if (guid != nullFileGuid)
			{
				// 이미 프로파일이 존재하는 경우
				if (volumeComponent->m_volumeProfileGuid != nullFileGuid)
				{
					Debug->LogWarning("Volume profile already exists. Replacing with new profile.");
				}
				volumeComponent->m_volumeProfileGuid = guid;
				volumeComponent->LoadProfile(guid);
			}
			else
			{
				Debug->LogError("Failed to load volume profile: " + filepath.string());
			}
		}
		ImGui::EndDragDropTarget();
	}

	if(volumeComponent->IsProfileLoaded())
	{
		VolumeProfile& profile = volumeComponent->GetVolumeProfile();

		if (ImGui::CollapsingHeader("ShadowPass"))
		{
			ImGui::PushID("ShadowPass");
			auto type = Meta::Find("ShadowMapPassSetting");
			Meta::DrawProperties(&profile.settings.shadow, *type);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("SSAOPass"))
		{
			ImGui::PushID("SSAOPass");
			auto type = Meta::Find("SSAOPassSetting");
			Meta::DrawProperties(&profile.settings.ssao, *type);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("DeferredPass"))
		{
			ImGui::PushID("DeferredPass");
			auto type = Meta::Find("DeferredPassSetting");
			Meta::DrawProperties(&profile.settings.deferred, *type);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("SSGIPass"))
		{
			ImGui::PushID("SSGIPass");
			auto type = Meta::Find("SSGIPassSetting");
			Meta::DrawProperties(&profile.settings.ssgi, *type);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("SkyBoxPass"))
		{
			file::path HDRPath = PathFinder::Relative("HDR\\");
			std::string_view profileTextureName = profile.settings.skyboxTextureName;
			std::string_view settingsTextureName = EngineSettingInstance->GetRenderPassSettings().skyboxTextureName;
			if (!settingsTextureName.empty() && settingsTextureName != profileTextureName)
			{
				profile.settings.skyboxTextureName = settingsTextureName;
			}
			
			if (profile.settings.skyboxTextureName.empty())
			{
				ImGui::Text("Drag HDR Texture Here");
			}
			else
			{
				ImGui::Text("Loaded HDR: %s", profile.settings.skyboxTextureName.c_str());
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HDR"))
				{
					const char* droppedFilePath = static_cast<const char*>(payload->Data);
					file::path filename = file::path(droppedFilePath).filename();
					file::path filepath = PathFinder::Relative("HDR\\") / filename;
					FileGuid guid = DataSystems->GetFileGuid(filepath);
					if (guid != nullFileGuid)
					{
						profile.settings.skyboxTextureName = filename.string();
					}
					else
					{
						Debug->LogError("Failed to load HDR: " + filepath.string());
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::Separator();
		if (ImGui::CollapsingHeader("PostProcessPass"))
		{
			if (ImGui::CollapsingHeader("AAPass"))
			{
				ImGui::PushID("AAPass");
				auto type = Meta::Find("AAPassSetting");
				Meta::DrawProperties(&profile.settings.aa, *type);
				ImGui::PopID();
			}

			if (ImGui::CollapsingHeader("BloomPass"))
			{
				ImGui::PushID("BloomPass");
				auto type = Meta::Find("BloomPassSetting");
				Meta::DrawProperties(&profile.settings.bloom, *type);
				ImGui::PopID();
			}

			//if (ImGui::CollapsingHeader("ScreenSpaceReflectionPass"))
			//{
			//	m_sceneRenderer->m_pScreenSpaceReflectionPass->ControlPanel();
			//}

			//if (ImGui::CollapsingHeader("SubsurfaceScatteringPass"))
			//{
			//	m_sceneRenderer->m_pSubsurfaceScatteringPass->ControlPanel();
			//}

			if (ImGui::CollapsingHeader("VignettePass"))
			{
				ImGui::PushID("VignettePass");
				auto type = Meta::Find("VignettePassSetting");
				Meta::DrawProperties(&profile.settings.vignette, *type);
				ImGui::PopID();
			}

			if (ImGui::CollapsingHeader("ToneMapPass"))
			{
				auto& setting = profile.settings.toneMap;

				ImGui::PushID("ToneMapPass");

				ImGui::Checkbox("Use ToneMap", &setting.isAbleToneMap);
				ImGui::Combo("ToneMap Type", &setting.toneMapType, "Reinhard\0ACES\0Uncharted2\0HDR10\0ACESFlim");

				ImGui::Separator();
				ImGui::Text("Auto Exposure Settings");
				ImGui::Checkbox("Use Auto Exposure", &setting.isAbleAutoExposure);

				ImGuiSliderFlags exposureFlags = setting.isAbleAutoExposure ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None;
				ImGui::DragFloat("ToneMap Exposure", &setting.toneMapExposure, 0.01f, 0.0f, 5.0f, "%.3f", exposureFlags);

				ImGui::Separator();
				ImGui::Text("Manual Camera Settings");

				ImGui::DragFloat("fNumber", &setting.fNumber, 0.01f, 1.0f, 32.0f);
				ImGui::DragFloat("Shutter Time", &setting.shutterTime, 0.001f, 0.000125f, 30.0f);
				ImGui::DragFloat("ISO", &setting.ISO, 50.0f, 50.0f, 6400.0f);
				ImGui::DragFloat("Exposure Compensation", &setting.exposureCompensation, 0.01f, -5.0f, 5.0f);
				ImGui::DragFloat("Speed Brightness", &setting.speedBrightness, 0.01f, 0.1f, 10.0f);
				ImGui::DragFloat("Speed Darkness", &setting.speedDarkness, 0.01f, 0.1f, 10.0f);

				ImGui::PopID();
			}

			if (ImGui::CollapsingHeader("ColorGradingPass"))
			{
				ImGui::PushID("ColorGradingPass");
				auto type = Meta::Find("ColorGradingPassSetting");
				Meta::DrawProperties(&profile.settings.colorGrading, *type);
				ImGui::PopID();
			}

			//if (ImGui::CollapsingHeader("VolumetricFogPass"))
			//{
			//	m_sceneRenderer->m_pVolumetricFogPass->ControlPanel();
			//}
		}

		volumeComponent->UpdateProfileEditMode();

		ImGui::Separator();
		if (ImGui::Button("Save VolumeProfile Asset"))
		{
			DataSystems->SaveExistVolumeProfile(volumeComponent->m_volumeProfileGuid, &profile);
		}
	}
}

#endif // !DYNAMICCPP_EXPORTS
