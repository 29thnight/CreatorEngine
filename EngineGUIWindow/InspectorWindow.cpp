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
#include "RectTransformComponent.h"
#include "DecalComponent.h"
#include "SpriteRenderer.h"
//----------------------------

#include "IconsFontAwesome6.h"
#include "fa.h"
#include "PinHelper.h"
#include "TableAPIHelper.h"
#include "NodeEditor.h"
#include <algorithm>
#include "imgui_stdlib.h"

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
			ImGuiDrawHelperGameObjectBaseInfo(selectedSceneObject);
			if (RectTransformComponent* rectTransform = selectedSceneObject->GetComponent<RectTransformComponent>())
			{
				ImGuiDrawHelperRectTransformComponent(rectTransform);
			}
			else
			{
				ImGuiDrawHelperTransformComponent(selectedSceneObject);
			}

			static bool isOpen = false;
			static Component* selectedComponent = nullptr;

			for (auto& component : selectedSceneObject->m_components)
			{
				if(nullptr == component || component->GetTypeID() == type_guid(RectTransformComponent))
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
					auto componentTypeID = component->GetTypeID();
					if(componentTypeID == type_guid(MeshRenderer))
					{
						MeshRenderer* meshRenderer = dynamic_cast<MeshRenderer*>(component.get());
						if (nullptr != meshRenderer)
						{
							ImGuiDrawHelperMeshRenderer(meshRenderer);
						}
					}
					else if (componentTypeID == type_guid(TerrainComponent)) {

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
					else if (componentTypeID == type_guid(Animator))
					{
						Animator* animator = dynamic_cast<Animator*> (component.get());
						if (nullptr != animator)
						{
							ImGuiDrawHelperAnimator(animator);
						}
					}
					else if (componentTypeID == type_guid(StateMachineComponent))
					{
						StateMachineComponent* fsm = dynamic_cast<StateMachineComponent*>(component.get());
						if (nullptr != fsm)
						{
							ImGuiDrawHelperFSM(fsm);
						}
					}
					else if (componentTypeID == type_guid(BehaviorTreeComponent))
					{
						BehaviorTreeComponent* bt = dynamic_cast<BehaviorTreeComponent*>(component.get());
						if (nullptr != bt)
						{
							ImGuiDrawHelperBT(bt);
						}
					}
					else if (componentTypeID == type_guid(PlayerInputComponent))
					{
						PlayerInputComponent* input = dynamic_cast<PlayerInputComponent*>(component.get());
						if (nullptr != input)
						{
							ImGuiDrawHelperPlayerInput(input);
						}
					}
					else if (componentTypeID == type_guid(VolumeComponent))
					{
						VolumeComponent* input = dynamic_cast<VolumeComponent*>(component.get());
						if (nullptr != input)
						{
							ImGuiDrawHelperVolume(input);
						}
					}
					else if (componentTypeID == type_guid(DecalComponent)) 
					{
						DecalComponent* input = dynamic_cast<DecalComponent*>(component.get());
						if (nullptr != input) 
						{
							ImGuiDrawHelperDecal(input);
						}
					}
					else if (componentTypeID == type_guid(ImageComponent))
					{
						ImageComponent* image = dynamic_cast<ImageComponent*>(component.get());
						if (nullptr != image)
						{
							ImGuiDrawHelperImageComponent(image);
						}
					}
					else if (componentTypeID == type_guid(SpriteRenderer))
					{
						SpriteRenderer* sprite = dynamic_cast<SpriteRenderer*>(component.get());
						if (nullptr != sprite)
						{
							//이건 뭔 버그죠?
							ImGuiDrawHelperSpriteRenderer(sprite);
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

void InspectorWindow::ImGuiDrawHelperGameObjectBaseInfo(GameObject* gameObject)
{
	std::string name = gameObject->m_name.ToString();
	bool isEnabled = gameObject->m_isEnabled;
	ImGui::Checkbox("##Enabled", &isEnabled);
	ImGui::SameLine();

	gameObject->SetEnabled(isEnabled);

	if (ImGui::InputText("##name",
		&name[0],
		name.capacity() + 1,
		ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
		Meta::InputTextCallback,
		static_cast<void*>(&name)))
	{
		gameObject->m_name.SetString(name);
	}

	ImGui::SameLine();
	ImGui::Checkbox("Static", &gameObject->m_isStatic);

	auto& tags = TagManagers->GetTags();
	auto& layers = TagManagers->GetLayers();
	int tagCount = static_cast<int>(tags.size());
	int layerCount = static_cast<int>(layers.size());
	static int prevTagCount = 0;
	static int prevLayerCount = 0;
	static int selectedTagIndex = 0;
	static int selectedLayerIndex = 0;

	static const char* tagNames[64]{};
	if (0 == prevTagCount || tagCount != prevTagCount)
	{
		memset(tagNames, 0, sizeof(tagNames));
		for (int i = 0; i < tagCount; ++i) {
			tagNames[i] = tags[i].c_str(); // Assuming TagManager::GetTags() returns a vector of strings
		}
	}

	static const char* layerNames[64]{};
	if (0 == prevLayerCount || layerCount != prevLayerCount)
	{
		memset(layerNames, 0, sizeof(layerNames));
		for (int i = 0; i < layerCount; ++i) {
			layerNames[i] = layers[i].c_str(); // Assuming TagManager::GetLayers() returns a vector of strings
		}
	}

	auto& selectedTag = gameObject->m_tag;
	auto& selectedLayer = gameObject->m_layer;

	selectedTagIndex = TagManagers->GetTagIndex(selectedTag.ToString());
	selectedLayerIndex = TagManagers->GetLayerIndex(selectedLayer.ToString());
	if (selectedTagIndex < 0 || selectedTagIndex >= tagCount)
	{
		selectedTagIndex = 0; // 기본값으로 첫 번째 태그 선택
	}
	// Tag 콤보박스
	ImGui::Text("Tag");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f); // 픽셀 단위로 너비 설정
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
					TagManagers->RemoveTagFromObject(selectedTag.ToString(), gameObject);
					selectedTag = tagNames[i];
					TagManagers->AddTagToObject(selectedTag.ToString(), gameObject);
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
	ImGui::SetNextItemWidth(90.0f); // 픽셀 단위로 너비 설정
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
					TagManagers->RemoveObjectFromLayer(selectedLayer.ToString(), gameObject);
					selectedLayer = layerNames[i];
					gameObject->SetCollisionType(); // 충돌 타입 업데이트
					TagManagers->AddObjectToLayer(selectedLayer.ToString(), gameObject);
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
}

void InspectorWindow::ImGuiDrawHelperTransformComponent(GameObject* gameObject)
{
	// 현재 트랜스폼 값
	Mathf::Vector4& position = gameObject->m_transform.position;
	Mathf::Vector4& rotation = gameObject->m_transform.rotation;
	Mathf::Vector4& scale = gameObject->m_transform.scale;

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
			gameObject->m_transform.SetDirty();
		}
		if (editingPosition && ImGui::IsItemDeactivatedAfterEdit())
		{
			if (prevPosition != position)
			{
				Meta::MakeCustomChangeCommand([=]
				{
					gameObject->m_transform.position = prevPosition;
					gameObject->m_transform.SetDirty();
				},
				[=]
				{
					gameObject->m_transform.position = position;
					gameObject->m_transform.SetDirty();
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
			gameObject->m_transform.SetDirty();
		}
		if (editingRotation && ImGui::IsItemDeactivatedAfterEdit())
		{
			Mathf::Vector3 prevEulerRad(prevEuler[0] * Mathf::Deg2Rad, prevEuler[1] * Mathf::Deg2Rad, prevEuler[2] * Mathf::Deg2Rad);
			Mathf::Vector4 compare = XMQuaternionRotationRollPitchYaw(prevEulerRad.x, prevEulerRad.y, prevEulerRad.z);
			if (compare != rotation)
			{
				Meta::MakeCustomChangeCommand([=]
				{
					gameObject->m_transform.rotation = prevRotation;
					gameObject->m_transform.SetDirty();
				},
				[=]
				{
					gameObject->m_transform.rotation = rotation;
					gameObject->m_transform.SetDirty();
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
			gameObject->m_transform.SetDirty();
		}
		if (editingScale && ImGui::IsItemDeactivatedAfterEdit())
		{
			if (prevScale != scale)
			{
				Meta::MakeCustomChangeCommand([=]
				{
					gameObject->m_transform.scale = prevScale;
					gameObject->m_transform.SetDirty();
				},
				[=]
				{
					gameObject->m_transform.scale = scale;
					gameObject->m_transform.SetDirty();
				});
			}
			editingScale = false;
		}

		{
			gameObject->m_transform.UpdateLocalMatrix();
		}
	}

	if (menuClicked) {
		ImGui::OpenPopup("TransformMenu");
		menuClicked = false;
	}

	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.0f);
	if (ImGui::BeginPopup("TransformMenu")) 
	{
		if (ImGui::MenuItem("Reset Transform"))
		{
			gameObject->m_transform.position = { 0, 0, 0, 1 };
			gameObject->m_transform.rotation = XMQuaternionIdentity();
			gameObject->m_transform.scale = { 1, 1, 1, 1 };
			gameObject->m_transform.SetDirty();
			gameObject->m_transform.UpdateLocalMatrix();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);
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
			ImGui::Checkbox("Use SkyBox", &profile.settings.m_isSkyboxEnabled);

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

void InspectorWindow::ImGuiDrawHelperDecal(DecalComponent* decalComponent)
{
	int sliceX = decalComponent->sliceX;
	int sliceY = decalComponent->sliceY;
	ImGui::SliderInt("SliceX", &sliceX, 1, 20);
	ImGui::SliderInt("SliceY", &sliceY, 1, 20);
	ImGui::InputInt("SliceNumber", &decalComponent->sliceNumber);
	decalComponent->sliceX = sliceX;
	decalComponent->sliceY = sliceY;

	ImGui::Checkbox("Use Animation", &decalComponent->useAnimation);
	if (decalComponent->useAnimation) {
		ImGui::SliderFloat("SlicePerSeconds", &decalComponent->slicePerSeconds, 0.f, 10.f, "%.5f");
		ImGui::Checkbox("isLoop", &decalComponent->isLoop);
	}

	if (decalComponent->GetDecalTexture() == nullptr)
		ImGui::Button("None Diffuse Texture", ImVec2(150, 20));
	else
		ImGui::Image((ImTextureID)decalComponent->GetDecalTexture()->m_pSRV, ImVec2(30, 30));
	ImVec2 minRect = ImGui::GetItemRectMin();
	ImVec2 maxRect = ImGui::GetItemRectMax();
	ImRect bb(minRect, maxRect);
	if (ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget"))) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)payload->Data;
			file::path filename = droppedFilePath;
			file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
			HashingString path = filepath.string();
			if (!filename.filename().empty()) {
				decalComponent->SetDecalTexture(filename.string().c_str());
			}
			else {
				Debug->Log("Empty Texture File Name");
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (decalComponent->GetNormalTexture() == nullptr)
		ImGui::Button("None Normal Texture", ImVec2(150, 20));
	else
		ImGui::Image((ImTextureID)decalComponent->GetNormalTexture()->m_pSRV, ImVec2(30, 30));
	minRect = ImGui::GetItemRectMin();
	maxRect = ImGui::GetItemRectMax();
	bb = { minRect, maxRect };
	if (ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget"))) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)payload->Data;
			file::path filename = droppedFilePath;
			file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
			HashingString path = filepath.string();
			if (!filename.filename().empty()) {
				decalComponent->SetNormalTexture(filename.string().c_str());
			}
			else {
				Debug->Log("Empty Texture File Name");
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (decalComponent->GetORMTexture() == nullptr)
		ImGui::Button("None OccluRoughMetal Texture", ImVec2(150, 20));
	else
		ImGui::Image((ImTextureID)decalComponent->GetORMTexture()->m_pSRV, ImVec2(30, 30));
	minRect = ImGui::GetItemRectMin();
	maxRect = ImGui::GetItemRectMax();
	bb = { minRect, maxRect };
	if (ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget"))) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)payload->Data;
			file::path filename = droppedFilePath;
			file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
			HashingString path = filepath.string();
			if (!filename.filename().empty()) {
				decalComponent->SetORMTexture(filename.string().c_str());
			}
			else {
				Debug->Log("Empty Texture File Name");
			}
		}
		ImGui::EndDragDropTarget();
	}
}

void InspectorWindow::ImGuiDrawHelperImageComponent(ImageComponent* imageComponent)
{
	auto textures = imageComponent->GetTextures();
	int count = static_cast<int>(textures.size());
	static int currentTextureIndex = imageComponent->curindex;

	if (count > 0)
	{
		const char* items[64]{};
		for (int i = 0; i < count; ++i)
		{
			items[i] = textures[i]->m_name.c_str();
		}
		ImGui::Text("Image");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f); // 픽셀 단위로 너비 설정
		if (ImGui::BeginCombo("##TextureCombo", items[currentTextureIndex]))
		{
			for (int i = 0; i < count; ++i)
			{
				bool isSelected = (currentTextureIndex == i);
				if (ImGui::Selectable(items[i], isSelected))
				{
					currentTextureIndex = i;
					imageComponent->SetTexture(i);
				}
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}
	else
	{
		ImGui::Text("No textures available. Please drag and drop a texture.");
	}

	ImGui::Text("Drag Texture Here");
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_TEXTURE"))
		{
			const char* droppedFilePath = static_cast<const char*>(payload->Data);
			file::path filename = file::path(droppedFilePath).filename();
			file::path filepath = PathFinder::Relative("UI\\") / filename;
			auto texture = DataSystems->LoadSharedTexture(filepath.string().c_str(),
				DataSystem::TextureFileType::UITexture);
			if (texture)
			{
				imageComponent->Load(texture);
				imageComponent->SetTexture(static_cast<int>(imageComponent->GetTextures().size() - 1));
				currentTextureIndex = static_cast<int>(imageComponent->GetTextures().size() - 1);
			}
			else
			{
				Debug->LogError("Failed to load UI Texture: " + filepath.string());
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SeparatorText("BaseInfo");
	ImGui::ColorEdit4("color tint", &imageComponent->color.x);
	ImGui::DragFloat("rotation", &imageComponent->rotate, 0.1f, -360.0f, 360.0f);
	ImGui::DragFloat2("origin", &imageComponent->origin.x, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("union scale", &imageComponent->unionScale, 0.01f, 1.f, 10.f);
	ImGui::InputInt("layer", &imageComponent->_layerorder);
	if(ImGui::Button("Reset Size", ImVec2(100, 20)))
	{
		imageComponent->ResetSize();
	}
	static const char* clipDirections[] = { "None", "LeftToRight", "RightToLeft", "UpToBottom", "BottomToTop" };

	int currentClipDir = static_cast<int>(imageComponent->clipDirection);
	ImGui::Combo("Clip Direction", &currentClipDir, clipDirections, IM_ARRAYSIZE(clipDirections));
	imageComponent->clipDirection = static_cast<ClipDirection>(currentClipDir);

	ImGui::DragFloat("Clip Percent", &imageComponent->clipPercent, 0.01f, 0.0f, 1.0f);

	std::string shaderName = "None";
	if (!imageComponent->GetCustomPixelShader().empty())
	{
		shaderName = imageComponent->GetCustomPixelShader();
	}
	ImGui::Text("Pixel Shader");
	ImGui::SameLine();
	ImGui::Button(shaderName.c_str(), ImVec2(150, 20));
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_BOX "##SelectShader"))
	{
		ShaderSystem->SetImageSelectionTarget(imageComponent);
		ImGui::GetContext("SelectImageCustomShader").Open();
	}
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TRASH "##RemoveShader"))
	{
		imageComponent->ClearCustomPixelShader();
	}

	ImGui::Text("Navigation");
	auto originNaviContainer = imageComponent->GetNavigations();
	std::array<Navigation, 4> naviContainer{}; // 복사본 생성

	for (auto navi : originNaviContainer)
	{
		naviContainer[navi.mode] = navi;
	}

	ImGui::Button(ICON_FA_ARROW_LEFT "##Left", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				GameObject* draggedObject = GameObject::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Left, draggedObject->shared_from_this());
			}
		}
	}
	ImGui::SameLine();
	Navigation leftNavi = naviContainer[(int)Direction::Left];
	if (leftNavi.navObject.m_ID_Data != HashedGuid::INVAILD_ID)
	{
		auto obj = GameObject::FindInstanceID(leftNavi.navObject);
		if (obj)
		{
			ImGui::Text(("-> " + obj->m_name.ToString()).c_str());
		}
		else
		{
			ImGui::Text("-> None");
		}
	}
	else
	{
		ImGui::Text("-> None");
	}

	ImGui::Button(ICON_FA_ARROW_RIGHT "##Right", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				GameObject* draggedObject = GameObject::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Right, draggedObject->shared_from_this());
			}
		}
	}
	ImGui::SameLine();
	Navigation rightNavi = naviContainer[(int)Direction::Right];
	if (rightNavi.navObject.m_ID_Data != HashedGuid::INVAILD_ID)
	{
		auto obj = GameObject::FindInstanceID(rightNavi.navObject);
		if (obj)
		{
			ImGui::Text(("-> " + obj->m_name.ToString()).c_str());
		}
		else
		{
			ImGui::Text("-> None");
		}
	}
	else
	{
		ImGui::Text("-> None");
	}

	ImGui::Button(ICON_FA_ARROW_UP "##Up", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				GameObject* draggedObject = GameObject::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Up, draggedObject->shared_from_this());
			}
		}
	}
	ImGui::SameLine();
	Navigation upNavi = naviContainer[(int)Direction::Up];
	if (upNavi.navObject.m_ID_Data != HashedGuid::INVAILD_ID)
	{
		auto obj = GameObject::FindInstanceID(upNavi.navObject);
		if (obj)
		{
			ImGui::Text(("-> " + obj->m_name.ToString()).c_str());
		}
		else
		{
			ImGui::Text("-> None");
		}
	}
	else
	{
		ImGui::Text("-> None");
	}
	ImGui::Button(ICON_FA_ARROW_DOWN "##Down", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			GameObject::Index draggedIndex = *(GameObject::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				GameObject* draggedObject = GameObject::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Down, draggedObject->shared_from_this());
			}
		}
	}
	ImGui::SameLine();
	Navigation downNavi = naviContainer[(int)Direction::Down];
	if (downNavi.navObject.m_ID_Data != HashedGuid::INVAILD_ID)
	{
		auto obj = GameObject::FindInstanceID(downNavi.navObject);
		if (obj)
		{
			ImGui::Text(("-> " + obj->m_name.ToString()).c_str());
		}
		else
		{
			ImGui::Text("-> None");
		}
	}
	else
	{
		ImGui::Text("-> None");
	}
}

void InspectorWindow::ImGuiDrawHelperSpriteRenderer(SpriteRenderer* spriteRenderer)
{
	if (spriteRenderer->GetSprite() == nullptr)
		ImGui::Button("None Sprite", ImVec2(150, 20));
	else
		ImGui::Image((ImTextureID)spriteRenderer->GetSprite()->m_pSRV, ImVec2(30, 30));
	ImVec2 minRect = ImGui::GetItemRectMin();
	ImVec2 maxRect = ImGui::GetItemRectMax();
	ImRect bb(minRect, maxRect);
	if (ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget")))
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)payload->Data;
			file::path filename = droppedFilePath;
			file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
			auto texture = DataSystems->LoadSharedTexture(filepath.string().c_str(), DataSystem::TextureFileType::Texture);
			spriteRenderer->SetSprite(texture);
		}
		ImGui::EndDragDropTarget();
	}

	if (const auto* type = Meta::Find("SpriteRenderer"))
	{
		Meta::DrawProperties(spriteRenderer, *type);
	}
}

#endif // !DYNAMICCPP_EXPORTS
