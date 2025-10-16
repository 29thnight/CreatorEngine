#ifndef DYNAMICCPP_EXPORTS
#include "InspectorWindow.h"
#include "SceneRenderer.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "GameObject.h"
#include "ICustomEditor.h"
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
#include "SoundManager.h"
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
#include "SoundComponent.h"
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

		// 3. 辦摹牖嬪 唸薑
		if (sceneObjectJustSelected)
		{
			// 啪歜 螃粽薛お 摹鷗 衛 YAML 摹鷗 п薯
			selectedNode = std::nullopt;
			isSelectedNode = false;
			wasMetaSelectedLastFrame = false;
		}

		if (metaNodeJustSelected)
		{
			// 詭顫 だ橾 摹鷗 衛 啪歜 螃粽薛お п薯
			selectedSceneObject = nullptr;
			prevSelectedSceneObject = nullptr;
			wasMetaSelectedLastFrame = true;
		}

		if (!selectedSceneObject && EngineSettingInstance->terrainBrush)
		{
			EngineSettingInstance->terrainBrush->m_isEditMode = false;
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

			if (!selectedSceneObject->HasComponent<TerrainComponent>() &&
				EngineSettingInstance->terrainBrush)
			{
				EngineSettingInstance->terrainBrush->m_isEditMode = false;
			}

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
						auto customInspector = std::dynamic_pointer_cast<ICustomEditor>(component);
						if (customInspector)
						{
							customInspector->OnInspectorGUI();
						}
						else
						{
							ImGuiDrawHelperModuleBehavior(moduleBehavior);
						}
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
							//檜勒 劃 幗斜轄?
							ImGuiDrawHelperSpriteRenderer(sprite);
						}
					}
					else if (componentTypeID == type_guid(Canvas))
					{
						Canvas* canvas = dynamic_cast<Canvas*>(component.get());
						if (nullptr != canvas)
						{
							ImGuiDrawHelperCanvas(canvas);
						}
					}
					else if (componentTypeID == type_guid(SoundComponent))
					{
						SoundComponent* snd = dynamic_cast<SoundComponent*>(component.get());
						if (snd) ImGuiDrawHelperSoundComponent(snd);   // 醴蝶籤 檣蝶め攪 ��轎
					}
					else if (type)
					{
						auto customInspector = std::dynamic_pointer_cast<ICustomEditor>(component);
						if (customInspector)
						{
							customInspector->OnInspectorGUI();
						}
						else
						{
							Meta::DrawObject(component.get(), *type);
						}
					}
				}
			}
			
			ImGui::Separator();
			ImVec2 windowSize = ImGui::GetWindowSize();      // ⑷營 孺紫辦曖 瞪羹 觼晦
			ImVec2 buttonSize = ImVec2(180, 0);              // 幗が 陛煎 觼晦 (撮煎朝 濠翕 啗骯脾)

			static ImGuiTextFilter searchFilter;

			ImGui::SetCursorPosX((windowSize.x - buttonSize.x) * 0.5f);  // 熱ゎ 醞懈 薑溺

			if (ImGui::Button("Add Component", buttonSize))
			{
				ImGui::OpenPopup("AddComponent");
			}

			ImGui::SetNextWindowSize(ImVec2(windowSize.x, 0)); // 錳ж朝 餌檜鍔 雖薑
			if (ImGui::BeginPopup("AddComponent"))
			{
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "Add Component"); // 喻塢儀 臢蝶お
				ImGui::Separator(); // 掘碟摹

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
						if (auto initializable = std::dynamic_pointer_cast<System::IInitializable>(component))
						{
							initializable->Initialize();
						}
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

			// 棻擠 Щ溯歜縑憮 翮晦
			if (m_openScriptPopup)
			{
				ImGui::OpenPopup("Scripts");
				m_openScriptPopup = false;
			}

			ImGui::SetNextWindowSize(ImVec2(windowSize.x, 0)); // 錳ж朝 餌檜鍔 雖薑
			if (ImGui::BeginPopup("Scripts"))
			{
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "ScriptComponent"); // 喻塢儀 臢蝶お
				ImGui::Separator(); // 掘碟摹

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

			// 棻擠 Щ溯歜縑憮 翮晦
			if (m_openNewScriptPopup)
			{
				ImGui::OpenPopup("NewScript");
				m_openNewScriptPopup = false;
			}

			if (m_openClipPicker) 
			{
				DrawSoundClipPicker();
			}

			if (isOpen)
			{
				ImGui::OpenPopup("ComponentMenu");
				isOpen = false;
			}

			ImGui::SetNextWindowSize(ImVec2(windowSize.x, 0)); // 錳ж朝 餌檜鍔 雖薑
			if (ImGui::BeginPopup("NewScript"))
			{
				ImGui::TextColored(ImVec4(1, 1, 1, 1), "New ScriptComponent"); // 喻塢儀 臢蝶お
				ImGui::Separator(); // 掘碟摹

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
		selectedTagIndex = 0; // 晦獄高戲煎 羅 廓簞 鷓斜 摹鷗
	}
	// Tag 巍爾夢蝶
	ImGui::Text("Tag");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f); // а撚 欽嬪煎 傘綠 撲薑
	if (ImGui::BeginCombo("##TagCombo", tagNames[selectedTagIndex]))
	{
		for (int i = 0; i <= tagCount; ++i)
		{
			bool isSelected = false;
			if (i == tagCount) // "Add Tag" о跡
			{
				if (ImGui::Selectable("Add Tag"))
				{
					m_openNewTagPopup = true; // で機 翮晦 Ы楚斜 撲薑
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
					selectedTagIndex = i; // 摹鷗脹 檣策蝶 機等檜お
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
	ImGui::SetNextItemWidth(90.0f); // а撚 欽嬪煎 傘綠 撲薑
	if (ImGui::BeginCombo("##LayerCombo", layerNames[selectedLayerIndex]))
	{
		for (int i = 0; i < layerCount; ++i)
		{
			bool isSelected = false;
			if (i == layerCount - 1) // "Add Layer" о跡
			{
				if (ImGui::Selectable("Add Layer"))
				{
					m_openNewLayerPopup = true; // で機 翮晦 Ы楚斜 撲薑
				}
			}
			else
			{
				isSelected = (selectedLayer == layerNames[i]);
				if (ImGui::Selectable(layerNames[i], isSelected))
				{
					TagManagers->RemoveObjectFromLayer(selectedLayer.ToString(), gameObject);
					selectedLayer = layerNames[i];
					gameObject->SetCollisionType(); // 醱給 顫殮 機等檜お
					TagManagers->AddObjectToLayer(selectedLayer.ToString(), gameObject);
					selectedLayerIndex = i; // 摹鷗脹 檣策蝶 機等檜お
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
		m_openNewTagPopup = false; // で機 翮晦 Ы楚斜 蟾晦��
	}

	if (m_openNewLayerPopup)
	{
		ImGui::OpenPopup("New Layer");
		m_openNewLayerPopup = false; // で機 翮晦 Ы楚斜 蟾晦��
	}

	// New Tag で機
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
				selectedTagIndex = tagCount; // 億煎 蹺陛脹 鷓斜 檣策蝶
				tagCount = TagManagers->GetTags().size(); // 鷓斜 偃熱 機等檜お
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

	// New Layer で機
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
				selectedLayerIndex = layerCount; // 億煎 蹺陛脹 溯檜橫 檣策蝶
				layerCount = TagManagers->GetLayers().size(); // 溯檜橫 偃熱 機等檜お
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
	// ⑷營 お楠蝶イ 高
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

	// Behavior Tree 檜葷 ル衛
	if (!BTComponent->name.empty())
	{
		ImGui::Text("Behavior Tree: %s", BTComponent->name.c_str());
	}

	//BlackBoard 檜葷 ル衛
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
				// 檜嘐 Щ煎だ橾檜 襄營ж朝 唳辦
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
		ImGui::SetNextItemWidth(150.0f); // а撚 欽嬪煎 傘綠 撲薑
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
	std::array<Navigation, 4> naviContainer{}; // 犒餌獄 儅撩

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

void InspectorWindow::ImGuiDrawHelperCanvas(Canvas* canvas)
{
	ImGui::InputText("CanvasName", &canvas->CanvasName);
	static int order{};

	order = canvas->CanvasOrder;
	if (ImGui::DragInt("CanvasOrder", &order))
	{
		canvas->SetCanvasOrder(order);
	}

}

void InspectorWindow::ImGuiDrawHelperSoundComponent(SoundComponent* sc)
{
	using namespace ImGui;

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	//  Clip / Picker
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	TextUnformatted("Clip");
	ImGui::SameLine();
	SetNextItemWidth(240);
	InputText("##ClipKeyRO", &sc->clipKey, ImGuiInputTextFlags_ReadOnly);
	ImGui::SameLine();
	if (Button(ICON_FA_FILE_AUDIO))
	{
		m_clipKeyCache = Sound->getAllClipKeys();
		m_clipSearch.clear();
		m_clipPickerTarget = sc;
		m_openClipPicker = true;
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	//  Bus / Basic Params
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	SeparatorText("Bus / Params");

	const char* busNames[] = { "BGM","SFX","PLAYER","MONSTER","UI" };
	int busIdx = (int)sc->bus;
	SetNextItemWidth(150);
	if (Combo("Bus", &busIdx, busNames, IM_ARRAYSIZE(busNames))) {
		sc->bus = (ChannelType)busIdx;
	}

	SetNextItemWidth(200);
	DragFloat("Volume", &sc->volume, 0.01f, 0.0f, 1.0f, "%.3f");
	SetNextItemWidth(200);
	DragFloat("Pitch", &sc->pitch, 0.01f, 0.25f, 4.0f, "%.2f");
	SetNextItemWidth(200);
	DragInt("Priority", &sc->priority, 1, 0, 256);

	bool loopBefore = sc->loop;
	Checkbox("Loop", &sc->loop); ImGui::SameLine();
	Checkbox("Play On Start", &sc->playOnStart);

	// 瑞Щ 鼻鷓 滲唳 闊衛 瓣割縑 奩艙
	if (loopBefore != sc->loop) {
		auto applyLoop = [&](FMOD::Channel* ch) {
			if (!ch) return;
			FMOD_MODE mode = FMOD_DEFAULT; ch->getMode(&mode);
			mode &= ~(FMOD_MODE)FMOD_LOOP_NORMAL;
			mode &= ~(FMOD_MODE)FMOD_LOOP_OFF;
			mode |= sc->loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;
			ch->setMode(mode);
			};
		applyLoop(sc->Get2DChannel());
		applyLoop(sc->Get3DChannel());
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	//  Spatial
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	SeparatorText("Spatial");
	Checkbox("Spatial (Blend 2D+3D)", &sc->spatial);

	if (sc->spatial) 
	{
		ImGui::SetNextItemWidth(220);
		ImGui::DragFloat("Spatial Blend", &sc->spatialBlend, 0.01f, 0.0f, 1.0f, "%.2f");

		float minBefore = sc->minDistance, maxBefore = sc->maxDistance;
		ImGui::SetNextItemWidth(220);
		ImGui::DragFloat("Min Distance", &sc->minDistance, 0.01f, 0.01f, 200.0f, "%.2f");
		ImGui::SetNextItemWidth(220);
		ImGui::DragFloat("Max Distance", &sc->maxDistance, 0.10f, 0.10f, 500.0f, "%.2f");
		if (sc->minDistance > sc->maxDistance) sc->maxDistance = sc->minDistance + 0.01f;

		const char* rolloffNames[] = { "Linear", "Inverse", "Custom" };
		int roll = (int)sc->rolloff;
		ImGui::SetNextItemWidth(180);
		bool rollChanged = ImGui::Combo("Rolloff", &roll, rolloffNames, IM_ARRAYSIZE(rolloffNames));
		sc->rolloff = (Rolloff)roll;

		// 斜楚Щ: spatial檜賊 о鼻 ル衛
		ImGui::SeparatorText("Distance Rolloff Curve");

		const bool isCustom = (sc->rolloff == Rolloff::Custom);

		// Linear /Inverse 摹鷗 衛: 濠翕 堊摹戲煎 翕晦��(檗晦瞪辨)
		if (!isCustom) {
			if (rollChanged || minBefore != sc->minDistance || maxBefore != sc->maxDistance || sc->localRolloffCurve.size() < 2) {
				if (sc->rolloff == Rolloff::Linear)  BuildLinearCurve(sc->localRolloffCurve, sc->minDistance, sc->maxDistance);
				if (sc->rolloff == Rolloff::Inverse) BuildInverseCurve(sc->localRolloffCurve, sc->minDistance, sc->maxDistance);
			}
			DrawRolloffCurveEditor(sc->localRolloffCurve, std::max(0.1f, sc->maxDistance), ImVec2(0, 200), nullptr, /*readOnly=*/true);
			ImGui::TextDisabled("Rolloff is %s - curve preview (read-only).", sc->rolloff == Rolloff::Linear ? "Linear" : "Inverse");
		}
		else {
			// Custom: 縑蛤お 陛棟
			if (sc->localRolloffCurve.size() < 2) {
				sc->localRolloffCurve = { {0.f,1.f}, { std::max(0.1f, sc->maxDistance), 0.f } };
			}
			// maxDistance 滲唳 衛 葆雖虞 薄 X蒂 彰嬪 頂煎 爾薑(ら餵 頂辨擎 嶸雖)
			sc->localRolloffCurve.back().distance = std::clamp(sc->localRolloffCurve.back().distance, 0.1f, std::max(0.1f, sc->maxDistance));

			if (ImGui::SmallButton("Reset to Default")) {
				sc->localRolloffCurve = { {0.f,1.f}, { std::max(0.1f, sc->maxDistance), 0.f } };
			}
			DrawRolloffCurveEditor(sc->localRolloffCurve, std::max(0.1f, sc->maxDistance), ImVec2(0, 200), nullptr, /*readOnly=*/false);
			ImGui::TextDisabled("Custom mode - drag points, double-click to add, right-click/Delete to remove.");
		}

		// 褒衛除 3D 瓣割 奩艙(嬪纂/剪葬/煤螃Щ 賅萄 蛔)
		if (auto* ch3 = sc->Get3DChannel()) {
			FMOD_VECTOR p{ sc->position.x, sc->position.y, sc->position.z };
			FMOD_VECTOR v{ sc->velocity.x, sc->velocity.y, sc->velocity.z };
			ch3->set3DAttributes(&p, &v);

			if (minBefore != sc->minDistance || maxBefore != sc->maxDistance || rollChanged) {
				FMOD_MODE mode = FMOD_DEFAULT | FMOD_3D | (sc->loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
				mode &= ~(FMOD_MODE)FMOD_3D_LINEARROLLOFF;
				mode &= ~(FMOD_MODE)FMOD_3D_INVERSEROLLOFF;
				mode &= ~(FMOD_MODE)FMOD_3D_CUSTOMROLLOFF;

				switch (sc->rolloff) {
				case Rolloff::Linear:  mode |= FMOD_3D_LINEARROLLOFF;  break;
				case Rolloff::Inverse: mode |= FMOD_3D_INVERSEROLLOFF; break;
				case Rolloff::Custom:  mode |= FMOD_3D_CUSTOMROLLOFF;  break;
				}
				ch3->setMode(mode);
				ch3->set3DMinMaxDistance(sc->minDistance, sc->maxDistance);
			}
		}
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	//  Reverb Send
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	SeparatorText("Reverb Send");
	bool useRevBefore = sc->useReverbSend;
	Checkbox("Enable Reverb Send", &sc->useReverbSend);

	// dB 蝸塭檜渦(-80~+10), 頂睡朝 摹⑽(0~1)煎 滲�納媦� FMOD縑 瞳辨
	SetNextItemWidth(260);
	DragFloat("Reverb Level (dB)", &sc->reverbLevel, 0.1f, -80.0f, 10.0f, "%.1f dB");
	SetNextItemWidth(200);
	DragInt("Reverb Index", &sc->reverbIndex, 1, 0, 3);

	auto applyReverb = [&](FMOD::Channel* ch) {
		if (!ch) return;
		if (!sc->useReverbSend) { ch->setReverbProperties(sc->reverbIndex, 0.0f); return; }

		// dB -> linear (0~1 clamp)
		float wet = powf(10.0f, sc->reverbLevel / 20.0f);
		wet = std::clamp(wet, 0.0f, 1.0f);
		ch->setReverbProperties(sc->reverbIndex, wet);
		};

	if (useRevBefore != sc->useReverbSend) {
		applyReverb(sc->Get2DChannel());
		applyReverb(sc->Get3DChannel());
	}
	// 高檜 夥船賊 о鼻 瞳辨
	if (IsItemEdited() || IsItemDeactivatedAfterEdit()) {
		applyReverb(sc->Get2DChannel());
		applyReverb(sc->Get3DChannel());
	}

	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	//  Preview Controls
	// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
	Separator();
	if (Button("Play")) { sc->Play(); } ImGui::SameLine();
	if (Button("Stop")) { sc->Stop(); } ImGui::SameLine();
	if (Button("OneShot")) { sc->PlayOneShot(); }

	// 獐睞/Я纂/Щ塭檜橫葬じ 滲唳 褒衛除 奩艙(瓣割 髦嬴氈擊 陽)
	auto applyBasic = [&](FMOD::Channel* ch) {
		if (!ch) return;
		ch->setVolume(sc->volume);
		ch->setPitch(sc->pitch);
		ch->setPriority(sc->priority);
		};
	applyBasic(sc->Get2DChannel());
	applyBasic(sc->Get3DChannel());
}

bool InspectorWindow::DrawRolloffCurveEditor(std::vector<CurvePoint>& curve, float maxDist, ImVec2 size, int* outSelected, bool readOnly)
{
	using namespace ImGui;
	if (curve.size() < 2) {
		curve = { {0.f, 1.f}, {std::max(0.1f, maxDist), 0.f} };
	}
	std::sort(curve.begin(), curve.end(),
		[](auto& a, auto& b) { return a.distance < b.distance; });

	if (size.x <= 0) size.x = GetContentRegionAvail().x;
	const ImVec2 p0 = GetCursorScreenPos();
	const ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
	const ImRect  rc(p0, p1);

	ImDrawList* dl = GetWindowDrawList();
	dl->AddRectFilled(rc.Min, rc.Max, GetColorU32(ImGuiCol_FrameBg));
	dl->AddRect(rc.Min, rc.Max, GetColorU32(ImGuiCol_Border));
	for (int i = 1; i < 4; i++) {
		float x = ImLerp(rc.Min.x, rc.Max.x, i / 4.f);
		float y = ImLerp(rc.Min.y, rc.Max.y, i / 4.f);
		dl->AddLine(ImVec2(x, rc.Min.y), ImVec2(x, rc.Max.y), GetColorU32(ImGuiCol_Separator), 1.f);
		dl->AddLine(ImVec2(rc.Min.x, y), ImVec2(rc.Max.x, y), GetColorU32(ImGuiCol_Separator), 1.f);
	}

	auto toScreen = [&](float dist, float gain) {
		float nx = (maxDist <= 0.0001f) ? 0.f : (dist / maxDist);
		float ny = 1.f - clamp01(gain);
		return ImVec2(ImLerp(rc.Min.x, rc.Max.x, clamp01(nx)),
			ImLerp(rc.Min.y, rc.Max.y, clamp01(ny)));
		};
	auto toData = [&](ImVec2 sp) {
		float nx = (sp.x - rc.Min.x) / std::max(1e-6f, (rc.Max.x - rc.Min.x));
		float ny = (sp.y - rc.Min.y) / std::max(1e-6f, (rc.Max.y - rc.Min.y));
		float dist = clamp01(nx) * std::max(0.0f, maxDist);
		float gain = clamp01(1.f - clamp01(ny));
		return std::pair<float, float>(dist, gain);
		};

	const ImVec2  mouse = GetIO().MousePos;
	const bool hovered = rc.Contains(mouse);
	const bool clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left) && !readOnly;
	const bool rclicked = hovered && IsMouseClicked(ImGuiMouseButton_Right) && !readOnly;
	const bool dclicked = hovered && IsMouseDoubleClicked(ImGuiMouseButton_Left) && !readOnly;

	static int  s_selected = -1;
	static bool s_dragging = false;
	if (outSelected) s_selected = *outSelected;

	const ImU32 lineCol = GetColorU32(ImGuiCol_PlotLines);
	for (size_t i = 1; i < curve.size(); ++i) {
		dl->AddLine(toScreen(curve[i - 1].distance, curve[i - 1].gain),
			toScreen(curve[i].distance, curve[i].gain), lineCol, 2.0f);
	}

	const float R = 5.f;
	const ImU32 handleCol = GetColorU32(ImGuiCol_PlotLinesHovered);
	int hoverIdx = -1;
	for (int i = 0; i < (int)curve.size(); ++i) {
		ImVec2 sp = toScreen(curve[i].distance, curve[i].gain);
		bool isHover = (ImLengthSqr(mouse - sp) <= (R + 2) * (R + 2));
		if (isHover) hoverIdx = i;
		dl->AddCircleFilled(sp, R, GetColorU32(i == s_selected ? ImGuiCol_PlotHistogramHovered :
			isHover ? ImGuiCol_PlotHistogram :
			ImGuiCol_ButtonHovered));
		dl->AddCircle(sp, R, handleCol);
	}

	if (!readOnly) {
		if (clicked) {
			if (hoverIdx >= 0) { s_selected = hoverIdx; s_dragging = true; }
			else { s_selected = -1; s_dragging = false; }
		}
		if (!IsMouseDown(ImGuiMouseButton_Left)) s_dragging = false;

		bool changed = false;
		if (s_dragging && s_selected >= 0) {
			bool lockX = (s_selected == 0 || s_selected == (int)curve.size() - 1);
			auto [nd, ng] = toData(mouse);
			if (lockX) nd = curve[s_selected].distance;
			ng = clamp01(ng);
			const float eps = 0.001f;
			if (!lockX) {
				float lo = (s_selected > 0) ? (curve[s_selected - 1].distance + eps) : 0.f;
				float hi = (s_selected < (int)curve.size() - 1) ? (curve[s_selected + 1].distance - eps) : maxDist;
				nd = std::clamp(nd, lo, hi);
			}
			if (curve[s_selected].distance != nd || curve[s_selected].gain != ng) {
				curve[s_selected].distance = nd;
				curve[s_selected].gain = ng;
				changed = true;
			}
		}

		if (dclicked) {
			auto [nd, ng] = toData(mouse);
			nd = std::clamp(nd, 0.f, std::max(0.f, maxDist));
			ng = clamp01(ng);
			int ins = (int)curve.size();
			for (int i = 1; i < (int)curve.size(); ++i) { if (nd <= curve[i].distance) { ins = i; break; } }
			curve.insert(curve.begin() + ins, { nd, ng });
			s_selected = ins;
			changed = true;
		}

		if ((rclicked || IsKeyPressed(ImGuiKey_Delete)) &&
			s_selected > 0 && s_selected < (int)curve.size() - 1) {
			curve.erase(curve.begin() + s_selected);
			s_selected = std::min(s_selected, (int)curve.size() - 1);
			changed = true;
		}

		if (hovered) {
			SetTooltip("L-Drag: Move  |  Double-Click: Add  |  Right-Click/Delete: Remove");
		}

		Dummy(size);
		if (outSelected) *outSelected = s_selected;
		return changed;
	}
	else {
		// readOnly 賅萄: 鼻��濛辨 橈擠, 寰頂虜
		if (hovered) SetTooltip("Graph is read-only (driven by Rolloff mode).");
		Dummy(size);
		if (outSelected) *outSelected = -1;
		return false;
	}
}

void InspectorWindow::DrawSoundClipPicker()
{
	using namespace ImGui;
	if (!m_openClipPicker) return;

	// 絮董 孺紫辦(賅殖 替釵)
	SetNextWindowSize(ImVec2(520, 480), ImGuiCond_Appearing);
	if (Begin("Select Audio Clip", &m_openClipPicker,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
	{
		// 鼻欽: 匐儀/葬Щ溯衛
		if (InputTextWithHint("##search", "Search clip key...", &m_clipSearch)) {
			// 殮溘 衛 闊衛 в攪 奩艙
		}
		SameLine();
		if (Button("Refresh")) {
			m_clipKeyCache = Sound->getAllClipKeys();
		}
		Separator();

		// в攪葭
		auto toLower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
		std::string q = toLower(m_clipSearch);

		// 葬蝶お 艙羲
		BeginChild("##cliplist", ImVec2(0, -48), true);
		static int selectedIndex = -1;
		const int N = (int)m_clipKeyCache.size();
		for (int i = 0; i < N; ++i) {
			const std::string& key = m_clipKeyCache[i];
			if (!q.empty() && toLower(key).find(q) == std::string::npos) continue;

			bool selected = (i == selectedIndex);
			if (Selectable(key.c_str(), selected)) {
				selectedIndex = i;
			}

			// 辦難 Щ葬箔 幗が
			if (IsItemHovered() && IsMouseDoubleClicked(0)) 
			{
				// 渦綰贗葛 = 摹鷗 �挨�
				if (m_clipPickerTarget && i >= 0) m_clipPickerTarget->clipKey = key;
				m_openClipPicker = false;
				selectedIndex = -1;
				break;
			}
			SameLine();
			if (SmallButton((ICON_FA_PLAY "##prev" + std::to_string(i)).c_str())) 
			{
				if (m_clipPickerTarget) 
				{
					// 嘐葬菔晦: ⑷營 顫啃 幗蝶/獐睞/Я纂 餌辨
					FMOD_VECTOR pos{ m_clipPickerTarget->position.x,
									 m_clipPickerTarget->position.y,
									 m_clipPickerTarget->position.z };

					FMOD_VECTOR vel{ m_clipPickerTarget->velocity.x,
									 m_clipPickerTarget->velocity.y,
									 m_clipPickerTarget->velocity.z };

					Sound->playOneShotPooled(
						key,
						m_clipPickerTarget->bus,
						m_clipPickerTarget->volume,
						m_clipPickerTarget->pitch,
						m_clipPickerTarget->priority,
						m_clipPickerTarget->spatial ? m_clipPickerTarget->spatialBlend : 0.0f,
						m_clipPickerTarget->spatial ? &pos : nullptr,
						m_clipPickerTarget->spatial ? &vel : nullptr,
						m_clipPickerTarget
					);
				}
			}
		}
		EndChild();

		// ж欽 幗が
		BeginDisabled(selectedIndex < 0);
		if (Button("Use")) 
		{
			if (m_clipPickerTarget && selectedIndex >= 0) 
			{
				m_clipPickerTarget->clipKey = m_clipKeyCache[selectedIndex];
			}
			m_openClipPicker = false;
			selectedIndex = -1;
		}
		EndDisabled();
		SameLine();
		if (Button("Close")) { m_openClipPicker = false; selectedIndex = -1; }
	}
	End();
}

#endif // !DYNAMICCPP_EXPORTS
