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
#include "ModuleBehavior.h"
#include "ComponentFactory.h"
#include "ReflectionImGuiHelper.h"
#include "CustomCollapsingHeader.h"
#include "Terrain.h"
#include "FileDialog.h"
#include "TagManager.h"
//----------------------------
#include "NodeFactory.h"
#include "StateMachineComponent.h"
#include "BehaviorTreeComponent.h"
#include "FunctionRegistry.h"
//----------------------------

#include "IconsFontAwesome6.h"
#include "fa.h"
#include "PinHelper.h"
#include "NodeEditor.h"
#include <algorithm>

namespace ed = ax::NodeEditor;
static const std::unordered_set<std::string> ignoredKeys = {
	"guid",
	"importSettings"
};

ed::EditorContext* m_fsmEditorContext{ nullptr };
bool			   s_CreatingLink = false;
ed::PinId		   s_LinkStartPin = 0;
ed::LinkId		   s_EditLinkId = 0;
bool			   s_RenameNodePopup{ false };

ed::EditorContext* s_BTEditorContext{ nullptr };

void DrawYamlNodeEditor(YAML::Node& node, const std::string& label = "")
{
	if (node.IsNull()) return;

	if (node.IsMap())
	{
		for (auto it = node.begin(); it != node.end(); ++it)
		{
			std::string key = it->first.as<std::string>();
			if (ignoredKeys.count(key)) continue;

			YAML::Node& value = it->second;

			if (value.IsMap() || value.IsSequence())
			{
				if (ImGui::TreeNode(key.c_str()))
				{
					DrawYamlNodeEditor(value, key);
					ImGui::TreePop();
				}
			}
			else
			{
				// 자동 분기 처리
				if (value.IsScalar())
				{
					std::string val = value.as<std::string>();
					std::istringstream iss(val);
					float f;
					int i;
					bool b;

					std::string uniqueID = key + "##" + label;

					// bool
					if (val == "true" || val == "false")
					{
						b = (val == "true");
						if (ImGui::Checkbox(uniqueID.c_str(), &b))
							value = b ? "true" : "false";
					}
					// int
					else if ((iss >> i) && iss.eof())
					{
						if (ImGui::InputInt(uniqueID.c_str(), &i))
							value = std::to_string(i);
					}
					// float
					else
					{
						std::istringstream iss2(val);
						if ((iss2 >> f) && iss2.eof())
						{
							if (ImGui::InputFloat(uniqueID.c_str(), &f))
								value = std::to_string(f);
						}
						else
						{
							// fallback to string
							char buffer[256];
							strcpy_s(buffer, val.c_str());
							if (ImGui::InputText(uniqueID.c_str(), buffer, sizeof(buffer)))
								value = std::string(buffer);
						}
					}
				}
			}
		}
	}
	else if (node.IsSequence())
	{
		for (std::size_t i = 0; i < node.size(); ++i)
		{
			YAML::Node element = node[i];
			std::string indexLabel = label + "[" + std::to_string(i) + "]";

			if (element.IsMap() || element.IsSequence())
			{
				if (ImGui::TreeNode(indexLabel.c_str()))
				{
					DrawYamlNodeEditor(element, indexLabel);
					ImGui::TreePop();
				}
			}
			else
			{
				std::string val = element.as<std::string>();
				std::istringstream iss(val);
				float f;
				int i;
				bool b;

				std::string uniqueID = indexLabel;

				// bool
				if (val == "true" || val == "false")
				{
					b = (val == "true");
					if (ImGui::Checkbox(uniqueID.c_str(), &b))
						element = b ? "true" : "false";
				}
				// int
				else if ((iss >> i) && iss.eof())
				{
					if (ImGui::InputInt(uniqueID.c_str(), &i))
						element = std::to_string(i);
				}
				// float
				else
				{
					std::istringstream iss2(val);
					if ((iss2 >> f) && iss2.eof())
					{
						if (ImGui::InputFloat(uniqueID.c_str(), &f))
							element = std::to_string(f);
					}
					else
					{
						char buffer[256];
						strcpy_s(buffer, val.c_str());
						if (ImGui::InputText(uniqueID.c_str(), buffer, sizeof(buffer)))
							element = std::string(buffer);
					}
				}
			}
		}
	}
}

#include "imgui-node-editor/imgui_node_editor.h"
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
				for (int i = 0; i < tagCount; ++i)
				{
					const bool isSelected = (selectedTag == tagNames[i]);
					if (ImGui::Selectable(tagNames[i], isSelected))
					{
						tag_manager->RemoveTagFromObject(selectedTag.ToString(), selectedSceneObject);
						selectedTag = tagNames[i];
						tag_manager->AddTagToObject(selectedTag.ToString(), selectedSceneObject);
						selectedTagIndex = i; // 선택된 인덱스 업데이트
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
					const bool isSelected = (selectedLayer == layerNames[i]);
					if (ImGui::Selectable(layerNames[i], isSelected))
					{
						tag_manager->RemoveObjectFromLayer(selectedLayer.ToString(), selectedSceneObject);
						selectedLayer = layerNames[i];
						tag_manager->AddObjectToLayer(selectedLayer.ToString(), selectedSceneObject);
						selectedLayerIndex = i; // 선택된 인덱스 업데이트
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			
			prevTagCount = tagCount;
			prevLayerCount = layerCount;

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

void InspectorWindow::ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer)
{
	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Element ");
		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

		if (meshRenderer->m_Material && !meshRenderer->m_Material->m_name.empty())
		{
			ImGui::Button(meshRenderer->m_Material->m_name.c_str(), ImVec2(250, 0));
		}
		else
		{
			ImGui::Button("No Material", ImVec2(250, 0));
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_BOX))
		{
			ImGui::GetContext("SelectMaterial").Open();
		}
		ImGui::PopStyleVar(2);
	}

	if (ImGui::CollapsingHeader("MaterialInfo", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const auto& mat_type = Meta::Find("Material");
		const auto& mat_info_type = Meta::Find("MaterialInfomation");
		if (nullptr != meshRenderer->m_Material)
		{
			Meta::DrawProperties(&meshRenderer->m_Material->m_materialInfo, *mat_info_type);
		}
		else
		{
			ImGui::Text("No Material assigned.");
		}
		const auto& mat_render_type = Meta::FindEnum("MaterialRenderingMode");
		for (auto& enumProp : mat_type->properties)
		{
			if (enumProp.typeID == TypeTrait::GUIDCreator::GetTypeID<MaterialRenderingMode>())
			{
				if (nullptr != meshRenderer->m_Material)
				{
					Meta::DrawEnumProperty((int*)&meshRenderer->m_Material->m_renderingMode, mat_render_type, enumProp);
				}
				else
				{
					ImGui::Text("No Material assigned.");
				}
				break;
			}
		}
	}

	if (ImGui::CollapsingHeader("LightMapping", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const auto& lightmap_type = Meta::Find("LightMapping");
		Meta::DrawProperties(&meshRenderer->m_LightMapping, *lightmap_type);
	}

	if (ImGui::CollapsingHeader("LODGroupShared", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// static 변수는 ImGui 컨텍스트 내에서 상태를 유지하기 위해 사용됩니다.
		// 여러 MeshRenderer를 동시에 편집하는 경우, 이 static 변수들을
		// 각 MeshRenderer 인스턴스에 연결된 상태로 관리해야 할 수 있습니다.
		static Mesh* selectedMesh = nullptr;
		static std::vector<float> LODThresholdsSetting; // 0.0f (Culled)는 포함하지 않음

		// 현재 선택된 Mesh가 변경되었는지 확인하고 LODThresholdsSetting을 업데이트
		if (selectedMesh != meshRenderer->m_Mesh)
		{
			selectedMesh = meshRenderer->m_Mesh;
			if (selectedMesh) // Mesh가 할당된 경우에만 LODThresholds를 가져옴
			{
				LODThresholdsSetting.clear();
				// GetLODThresholds는 0.0f (Culled)를 제외한 실제 LOD 임계값만 반환한다고 가정
				LODThresholdsSetting = selectedMesh->GetLODThresholds();
			}
			else
			{
				LODThresholdsSetting.clear(); // Mesh가 없으면 임계값도 비움
			}
		}

		if (!selectedMesh) // selectedMesh가 여전히 null인 경우 (meshRenderer->m_Mesh가 null)
		{
			ImGui::Text("No Mesh assigned.");
		}
		else
		{
			ImGui::Text("Mesh: %s", selectedMesh->GetName().c_str());
			ImGui::Checkbox("Enable LODGroup", &meshRenderer->m_isEnableLOD);

			if (meshRenderer->m_isEnableLOD)
			{
				// --- LOD 임계값 UI ---
				float barHeight = 30.0f;
				float barPadding = 20.0f; // 막대 좌우 패딩
				float barWidth = ImGui::GetContentRegionAvail().x - barPadding * 2;
				if (barWidth < 100.0f) barWidth = 100.0f; // 최소 너비

				ImVec2 p = ImGui::GetCursorScreenPos(); // 그리기 영역의 좌상단 위치
				ImVec2 barMin = ImVec2(p.x + barPadding, p.y);
				ImVec2 barMax = ImVec2(barMin.x + barWidth, barMin.y + barHeight);

				ImDrawList* draw_list = ImGui::GetWindowDrawList();

				// 배경 막대 그리기
				draw_list->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 50, 255)); // 어두운 회색 배경
				draw_list->AddRect(barMin, barMax, IM_COL32(100, 100, 100, 255)); // 테두리

				// LOD별 색상 (예시)
				ImU32 lodColors[] = {
					IM_COL32(0, 200, 0, 255),   // LOD0 (녹색)
					IM_COL32(200, 200, 0, 255), // LOD1 (노란색)
					IM_COL32(200, 0, 0, 255),   // LOD2 (빨간색)
					IM_COL32(100, 100, 100, 255) // Culled (회색) - 마지막 세그먼트용
				};
				const int numLODColors = sizeof(lodColors) / sizeof(lodColors[0]);

				// LOD 세그먼트 및 텍스트 그리기
				// 모든 경계값을 포함하는 벡터 (1.0f, LOD0_threshold, ..., LOD(N-1)_threshold, 0.0f)
				std::vector<float> all_boundaries;
				all_boundaries.push_back(1.0f); // LOD0의 시작 (가장 높은 디테일)
				for (float t : LODThresholdsSetting) {
					all_boundaries.push_back(t);
				}
				all_boundaries.push_back(0.0f); // Culled의 끝 (가장 낮은 디테일)

				float currentSegmentStartX = barMin.x;
				for (int i = 0; i < all_boundaries.size() - 1; ++i)
				{
					float leftBoundary = all_boundaries[i];
					float rightBoundary = all_boundaries[i + 1];

					// 현재 및 다음 임계값의 차이를 기반으로 세그먼트 너비 계산
					// (leftBoundary는 항상 rightBoundary보다 크거나 같음)
					float segmentPixelWidth = (leftBoundary - rightBoundary) * barWidth;

					ImVec2 segmentMin = ImVec2(currentSegmentStartX, barMin.y);
					ImVec2 segmentMax = ImVec2(currentSegmentStartX + segmentPixelWidth, barMax.y);

					// LOD 세그먼트 배경 그리기
					draw_list->AddRectFilled(segmentMin, segmentMax, lodColors[std::min(i, numLODColors - 1)]);
					draw_list->AddRect(segmentMin, segmentMax, IM_COL32(255, 255, 255, 100)); // 테두리

					// LOD 텍스트 그리기
					std::string lodText;
					if (i < LODThresholdsSetting.size()) {
						lodText = "LOD" + std::to_string(i);
					}
					else {
						lodText = "Culled"; // 마지막 세그먼트
					}
					ImVec2 textPos = ImVec2(segmentMin.x + 5, segmentMin.y + (barHeight - ImGui::GetTextLineHeight()) / 2);
					draw_list->AddText(textPos, IM_COL32(255, 255, 255, 255), lodText.c_str());

					currentSegmentStartX += segmentPixelWidth;
				}

				// 드래그 가능한 임계값 핸들 그리기 및 처리
				// 핸들은 LODThresholdsSetting의 각 요소에 해당하며, 이는 각 LOD의 오른쪽 경계입니다.
				for (int i = 0; i < LODThresholdsSetting.size(); ++i) // 0.0f (Culled)는 핸들이 없음
				{
					float handleWidth = 10.0f; // 드래그 가능한 핸들의 너비
					// 핸들 위치 계산: 1.0f가 바의 왼쪽 끝, 0.0f가 바의 오른쪽 끝
					float handleX = barMin.x + (1.0f - LODThresholdsSetting[i]) * barWidth;

					ImVec2 handleMin = ImVec2(handleX - handleWidth / 2, barMin.y);
					ImVec2 handleMax = ImVec2(handleX + handleWidth / 2, barMax.y);

					// 드래그를 위한 투명 버튼 생성
					ImGui::SetCursorScreenPos(handleMin);
					ImGui::InvisibleButton(("LOD_Handle_" + std::to_string(i)).c_str(), ImVec2(handleWidth, barHeight));

					bool isHovered = ImGui::IsItemHovered();
					bool isActive = ImGui::IsItemActive();

					// 핸들 그리기
					draw_list->AddRectFilled(handleMin, handleMax, isActive ? IM_COL32(255, 255, 0, 255) : (isHovered ? IM_COL32(200, 200, 0, 255) : IM_COL32(150, 150, 0, 255)));
					draw_list->AddRect(handleMin, handleMax, IM_COL32(255, 255, 255, 255)); // 테두리

					// 드래그 처리
					if (isActive && ImGui::IsMouseDragging(0))
					{
						float mouseDeltaX = ImGui::GetIO().MouseDelta.x;
						float deltaThreshold = mouseDeltaX / barWidth; // 0-1 범위의 델타

						// 유니티처럼 오른쪽으로 드래그하면 임계값이 감소하도록 변경
						float newThreshold = LODThresholdsSetting[i] - deltaThreshold;

						// 0.0f ~ 1.0f 범위로 클램프
						newThreshold = std::max(0.0f, std::min(1.0f, newThreshold));

						// 인접한 임계값과의 겹침 방지
						// newThreshold는 왼쪽 임계값(더 높은 값)보다 작아야 함
						if (i > 0) {
							newThreshold = std::min(newThreshold, LODThresholdsSetting[i - 1] - 0.001f);
						}
						// newThreshold는 오른쪽 임계값(더 낮은 값)보다 커야 함
						if (i < LODThresholdsSetting.size() - 1) {
							newThreshold = std::max(newThreshold, LODThresholdsSetting[i + 1] + 0.001f);
						}
						else { // 마지막 LOD 임계값은 0.0f보다 커야 함
							newThreshold = std::max(newThreshold, 0.0f + 0.001f);
						}

						LODThresholdsSetting[i] = newThreshold;
					}
				}

				// 그리기 영역을 지나 커서 위치 이동
				ImGui::Dummy(ImVec2(barWidth + barPadding * 2, barHeight));

				// 디버깅/정보를 위한 현재 임계값 표시
				ImGui::Text("LOD Thresholds:");
				for (int i = 0; i < LODThresholdsSetting.size(); ++i) {
					ImGui::SameLine();
					ImGui::Text("LOD%d: %.3f", i, LODThresholdsSetting[i]);
				}

				// LOD 추가/제거 버튼 (선택 사항, 유니티와 유사한 동작을 위해 유용)
				if (ImGui::Button("Add LOD")) {
					// 새로운 LOD를 추가합니다.
					float newThreshold;
					if (LODThresholdsSetting.empty()) {
						newThreshold = 0.5f; // 첫 LOD는 기본값 0.5f
					}
					else {
						// 마지막 LOD와 Culled (0.0f) 사이의 중간값
						newThreshold = (LODThresholdsSetting.back() + 0.0f) / 2.0f;
					}
					LODThresholdsSetting.push_back(newThreshold);
					// 내림차순 정렬 유지
					std::sort(LODThresholdsSetting.rbegin(), LODThresholdsSetting.rend());
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove Last LOD")) {
					if (!LODThresholdsSetting.empty()) { // Culled는 제거하지 않으므로, 실제 LOD가 하나라도 있어야 제거 가능
						LODThresholdsSetting.pop_back();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Apply LOD Thresholds"))
				{
					// LODThresholdsSetting을 selectedMesh->GenerateLODs에 직접 적용
					selectedMesh->GenerateLODs(LODThresholdsSetting);
					// Debug->Log는 예시이므로 실제 로깅 시스템에 맞게 변경하세요.
					// Debug->Log("Applied LOD Thresholds to Mesh: " + selectedMesh->GetName());
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("ShadowSetting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable Shadow Receive", &meshRenderer->m_shadowRecive);
		ImGui::Checkbox("Enable Shadow Cast", &meshRenderer->m_shadowCast);
	}

	if (DataSystems->m_trasfarMaterial)
	{
		std::string name{};
		if (meshRenderer->m_Material)
		{
			name = meshRenderer->m_Material->m_name;
		}
		Meta::MakeCustomChangeCommand(
		[=]
		{
			if(!name.empty() && DataSystems->Materials.find(name) != DataSystems->Materials.end())
			{
				meshRenderer->m_Material = DataSystems->Materials[name].get();
			}
			else
			{
				meshRenderer->m_Material = nullptr;
			}
		},
		[=]
		{
			meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
		});

		meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
		DataSystems->m_trasfarMaterial = nullptr;
	}
}

void InspectorWindow::ImGuiDrawHelperModuleBehavior(ModuleBehavior* moduleBehavior)
{
	if (moduleBehavior)
	{
		ImGui::Text("Script		 ");
		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
		if (ImGui::Button(moduleBehavior->GetHashedName().ToString().c_str(), ImVec2(250, 0)))
		{
			FileGuid guid = DataSystems->GetStemToGuid(moduleBehavior->GetHashedName().ToString());
			file::path scriptFullPath = DataSystems->GetFilePath(guid);
			if (scriptFullPath.empty())
			{
				Debug->LogError("Script not found: " + moduleBehavior->GetHashedName().ToString());
				ImGui::PopStyleVar(2);
				return;
			}

			file::path slnPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.sln");

			DataSystems->OpenSolutionAndFile(slnPath, scriptFullPath);
		}
		ImGui::PopStyleVar(2);

		const Meta::Type* type = Meta::Find(moduleBehavior->m_name.ToString());
		if (type)
		{
			Meta::DrawProperties(moduleBehavior, *type);
			Meta::DrawMethods(moduleBehavior, *type);
		}
		else
		{
			ImGui::Text("Script type not found: %s", moduleBehavior->m_name.ToString().c_str());
		}
	}
}

void InspectorWindow::ImGuiDrawHelperAnimator(Animator* animator)
{
	if (animator)
	{
		static bool showControllersWindow = false;
		const auto& aniType = Meta::Find(animator->GetTypeID());
		Meta::DrawProperties(animator, *aniType);
		Meta::DrawMethods(animator, *aniType);
		if (ImGui::CollapsingHeader("animations"))
		{
			for (auto& animation : animator->m_Skeleton->m_animations)
			{
				ImGui::PushID(animation.m_name.c_str());
				const auto& mat_info_type = Meta::Find("Animation");
				Meta::DrawProperties(&animation, *mat_info_type);

				ImGui::PopID();
			}
		}

		//if (!animator->m_animationControllers.empty())
		{
			ImGui::Text("Controllers ");
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_BOX))
			{
				showControllersWindow = !showControllersWindow;
			}
			if(showControllersWindow)
			{
		




				bool open = ImGui::Begin("Animation Controllers", &showControllersWindow);
				int i = 0;
				static int selectedControllerIndex = -1;
				static int preSelectIndex = -1;
				static int linkIndex = -1;
				static int ClickNodeIndex = -1;
				static int targetNodeIndex = -1;
				static int selectedTransitionIndex = -1;
				static int preInspectorIndex = -1; //인스펙터에뛰운 인덱스번호 
				static int AvatarControllerIndex = -1;
				static bool showAvatarMaskWindow = false;

				if (open && ImGui::IsWindowAppearing())
				{
					static int selectedControllerIndex = -1;
					static int preSelectIndex = -1;
					static int linkIndex = -1;
					static int ClickNodeIndex = -1;
					static int targetNodeIndex = -1;
					static int selectedTransitionIndex = -1;
					static int preInspectorIndex = -1; //인스펙터에뛰운 인덱스번호 
					static int AvatarControllerIndex = -1;
					static bool showAvatarMaskWindow = false;
				}

				auto& controllers = animator->m_animationControllers;
				ImGui::BeginChild("Leftpanel", ImVec2(200, 500), false); 
				if (ImGui::BeginTabBar("ControllerTabs", ImGuiTabBarFlags_None))
				{
					if (ImGui::BeginTabItem("Layers"))
					{
						ImGui::Separator();
						for (int index = 0; index < controllers.size(); ++index)
						{
							auto& controller = controllers[index];
							bool isSelected = (selectedControllerIndex == index);
							ImGui::PushID(index);

							if (ImGui::Selectable(controller->name.c_str(), true,0,ImVec2(150,0)))
							{
								preSelectIndex = selectedControllerIndex;
								selectedControllerIndex = index;
							}

							if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
							{
								ImGui::OpenPopup("RightClickMenu");
								selectedControllerIndex = index;
							}
							if (ImGui::BeginPopup("RightClickMenu"))
							{
								if (ImGui::MenuItem("Copy Contorller")) { /* 카피 컨트롤러 함수 */ }
								if (ImGui::MenuItem("Delete Controller")) 
								{  
									animator->DeleteController(selectedControllerIndex);
									selectedControllerIndex = -1;
								}
								ImGui::EndPopup();
							}
							ImGui::SameLine();
							if (ImGui::SmallButton(ICON_FA_CHESS_ROOK))
							{
								ImGui::OpenPopup("ControllerDetailPopup");
							}

							if (ImGui::BeginPopup("ControllerDetailPopup"))
							{
								ImGui::Text("Detail: %s", "ControllerDetailPopup");
								ImGui::Separator();

								char buffer[128];
								strcpy_s(buffer, controller->name.c_str());
								buffer[sizeof(buffer) - 1] = '\0';
								ImGui::Text("Name");
								ImGui::SameLine();
								if (ImGui::InputText("##Controller Name", buffer, sizeof(buffer)))
								{
									controller->name = buffer;
									std::string filename = controller->name + ".json";
									controller->m_nodeEditor->ReNameJson(filename);
								}

								ImGui::Text("Avatar Mask"); 
								ImGui::SameLine();
								if (controller->useMask)
								{
									if (ImGui::SmallButton(ICON_FA_PUZZLE_PIECE))
									{
										AvatarControllerIndex = index;
										showAvatarMaskWindow = !showAvatarMaskWindow;
									}
									ImGui::SameLine();

									if (ImGui::Button("Delete Avatar"))
									{
										controller->DeleteAvatarMask();
									}
								}
								else
								{
									if (ImGui::Button("Craete Avatar"))
									{
										controller->CreateMask();
									}
								}
								ImGui::Separator();
								ImGui::EndPopup();
							}
							ImGui::PopID();
						}

						if (showAvatarMaskWindow)
						{
							
							if (ImGui::Begin("AvatarMask", &showAvatarMaskWindow))
							{
								// 내용물 UI 작성
								ImGui::Text(controllers[AvatarControllerIndex]->name.c_str());
								ImGui::Separator();
								auto avatarMask = controllers[AvatarControllerIndex]->GetAvatarMask();
								ImGui::Checkbox("isHumaniod", &avatarMask->isHumanoid);
								ImGui::Separator();
								ImGui::Separator();
								if (avatarMask->isHumanoid)
								{
									ImGui::Checkbox("UseAll", &avatarMask->useAll);
									ImGui::Checkbox("UseUpper", &avatarMask->useUpper);
									ImGui::Checkbox("UseLower", &avatarMask->useLower);
								}
								else
								{
									auto& rootMask = avatarMask->RootMask;
									std::function<void(BoneMask*)> drawMaskTree;
									if (rootMask)
									{
										drawMaskTree = [&](BoneMask* mask)
											{
												// 고유 ID 만들기
												std::string label = mask->boneName + "##" + mask->boneName;

												// TreeNode는 펼칠 수 있는 드롭다운 역할
												if (ImGui::TreeNode(label.c_str()))
												{
													// Checkbox를 트리 노드 안에 표시
													ImGui::Checkbox(("Enable##" + mask->boneName).c_str(), &mask->isEnabled);

													for (auto& child : mask->m_children)
													{
														drawMaskTree(child); // 재귀 호출
													}

													ImGui::TreePop();
												}
											};
										drawMaskTree(rootMask);
									}
								}

							}
							ImGui::End(); 
						}
						ImGui::Separator();
						if (ImGui::Button("Create Layer"))
						{
							animator->CreateController_UI();
						}
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Parameters"))
					{
						ImGui::Separator();

						auto& parameters = animator->Parameters;
						ImGui::Text("parameter");
						ImGui::SameLine();
						if (ImGui::SmallButton(ICON_FA_PLUS))
						{
							ImGui::OpenPopup("AddParameterPopup");	
						}
						if (ImGui::BeginPopup("AddParameterPopup"))
						{
							if (ImGui::MenuItem("Add Float"))
							{
								animator->AddDefaultParameter(ValueType::Float);
							}
							if (ImGui::MenuItem("Add Int"))
							{
								animator->AddDefaultParameter(ValueType::Int);
							}
							if (ImGui::MenuItem("Add Bool"))
							{
								animator->AddDefaultParameter(ValueType::Bool);
							}
							if (ImGui::MenuItem("Add Trigger"))
							{
								animator->AddDefaultParameter(ValueType::Trigger);
							}
							ImGui::EndPopup();
						}
						ImGui::Separator();
						for (int index = 0; index < parameters.size(); ++index)
						{
							ImGui::PushID(index);
							auto& parameter = parameters[index];
							char buffer[128];
							strcpy_s(buffer, parameter->name.c_str());
							buffer[sizeof(buffer) - 1] = '\0';
							if(ImGui::InputText("", buffer, sizeof(buffer)))
							{
							
								for (auto& controller : controllers)
								{
									for (auto& state : controller->StateVec)
									{
										for (auto& transtion : state->Transitions)
										{
											for (auto& condition : transtion->conditions)
											{
												if (condition.valueName == parameter->name)
												{
													condition.valueName = buffer;
												}
											}
										}
									}
								}
								parameter->name = buffer;
							}
							ImGui::SameLine();
							if (ImGui::SmallButton(ICON_FA_MINUS))
							{
								animator->DeleteParameter(index);
							}

							ImGui::PopID();
						}

						ImGui::EndTabItem();
					}

					ImGui::EndTabBar();
				 }
				 AnimationController* controller = nullptr;
				 NodeEditor* nodeEdtior = nullptr;
					ImGui::EndChild();
					ImGui::SameLine();
					ImGui::BeginChild("Controller Info", ImVec2(1200, 500), false);
					if(!animator->m_animationControllers.empty() && selectedControllerIndex != -1)
						controller= animator->m_animationControllers[selectedControllerIndex].get();
					std::string controllerName;
					if (controller)
					{
						controllerName = controller->name + " Controller Info";
					}
					else
					{
						controllerName  = " Controller Info";
					}
					ImGui::Text(controllerName.c_str());
					ImGui::Separator();
					if (selectedControllerIndex >= 0 && selectedControllerIndex < animator->m_animationControllers.size())
					{
						controller = animator->m_animationControllers[selectedControllerIndex].get();
						nodeEdtior;
						static bool isOpenPopUp;
						static bool isOpenNodePopUp;
						if (preSelectIndex != selectedControllerIndex)
						{
							linkIndex = -1;
							ClickNodeIndex = -1;
							targetNodeIndex = -1;
							preSelectIndex = selectedControllerIndex;
							isOpenPopUp = false;
							isOpenNodePopUp = false;
						}
						std::string fileName = controller->name + ".json";
						{
							controller->m_nodeEditor->MakeEdit(fileName);

							for (auto& state : controller->StateVec)
							{
								controller->m_nodeEditor->MakeNode(state->m_name);
							}
							
							for (auto& state : controller->StateVec)
							{
								for (auto& trans : state->Transitions)
								{
									controller->m_nodeEditor->MakeLink(trans->GetCurState(), trans->GetNextState(), trans->m_name);	
								}
							}
							controller->m_nodeEditor->DrawLink(&linkIndex);
							controller->m_nodeEditor->DrawNode(&ClickNodeIndex);
							controller->m_nodeEditor->Update();
							
							if (targetNodeIndex != -1)
							{
								auto states = controller->StateVec;
								int curIndex = controller->m_nodeEditor->seletedCurNodeIndex;
								if (states[targetNodeIndex]->m_isAny == true) {}
								else
								{
									controller->CreateTransition(states[curIndex]->m_name, states[targetNodeIndex]->m_name);
								}
								targetNodeIndex = -1;
							}
							if (ClickNodeIndex != -1)
							{
								isOpenNodePopUp = true;
							}
							if (ed::ShowBackgroundContextMenu())
							{
								if (isOpenNodePopUp)
								{
									isOpenNodePopUp = false;
									ClickNodeIndex = -1;
								}
								isOpenPopUp = true;
							}
							else
							{
								isOpenPopUp = false;
							}
							controller->m_nodeEditor->EndEdit();
							if (isOpenNodePopUp)
							{
								ImGui::OpenPopup("NodeMenu");
							}
							if (ImGui::BeginPopup("NodeMenu"))
							{
								if (ImGui::MenuItem("Make Transition"))
								{
									controller->m_nodeEditor->MakeNewLink(&targetNodeIndex);
									isOpenNodePopUp = false;
									ClickNodeIndex = -1;
								}
								if (ImGui::MenuItem("Delete State"))
								{
									controller->DeleteState(controller->StateVec[ClickNodeIndex]->m_name);
									isOpenNodePopUp = false;
									
									if (ClickNodeIndex == controller->m_nodeEditor->seletedCurNodeIndex)
									{
										controller->m_nodeEditor->seletedCurNodeIndex = -1;
									}
									ClickNodeIndex = -1;
								}
								ImGui::EndPopup();
							}
							if (isOpenPopUp)
							{
								ImGui::OpenPopup("NodeEditorContextMenu");
							}
							if (ImGui::BeginPopup("NodeEditorContextMenu"))
							{
								if (ImGui::MenuItem("Add Node"))
								{
									controller->CreateState_UI();
									isOpenPopUp = false;
								}

								ImGui::EndPopup();
							}

							if (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
							{
								ImGui::CloseCurrentPopup();
								isOpenNodePopUp = false;
								ClickNodeIndex = -1;
							}
						}
					}

					ImGui::EndChild();
					ImGui::SameLine();
					ImGui::BeginChild("Inspector Info", ImVec2(0, 500), false);
					ImGui::Text("Inspector");
					ImGui::Separator();
					if (preSelectIndex != selectedControllerIndex)
					{
						linkIndex = -1;
						ClickNodeIndex = -1;
						targetNodeIndex = -1;
						preSelectIndex = selectedControllerIndex;
						
					}
					if (controller != nullptr && controller->m_nodeEditor->m_selectedType == SelectedType::Link && linkIndex != -1)
					{
						if (preInspectorIndex != linkIndex)
						{
							selectedTransitionIndex = -1;
						}
						preInspectorIndex = linkIndex;
						ImGui::Text("Transitions");
						ImGui::Separator();
						std::string fromNode = controller->m_nodeEditor->Links[linkIndex]->fromNode->name;
						std::string toNode = controller->m_nodeEditor->Links[linkIndex]->toNode->name;
						auto transitions = controller->FindState(fromNode)->FindTransitions(toNode);

						for (auto& transition : transitions)
						{
							std::string curStateName = transition->GetCurState();
							std::string nextStateName = transition->GetNextState();
							std::string transitionName = curStateName + " to " + nextStateName;
							if (ImGui::Selectable(transitionName.c_str(), true))
							{
								selectedTransitionIndex = i;
							}

							if (selectedTransitionIndex != -1)
							{
								auto& conditions = transition->conditions;
								ImGui::Separator();
								ImGui::Checkbox("HasExitTIme", &transition->hasExitTime);
								ImGui::SliderFloat("ExitTime", &transition->exitTime, 0.1f, 1.0f);
								ImGui::InputFloat("Transition Duration", &transition->blendTime);
								ImGui::Separator();
								ImGui::Separator();
								ImGui::Text("Conditions");
								ImGui::Separator();
								if (conditions.empty())
								{
									ImGui::Text("Empty Conditions");
								}
								else
								{
									for (int i = 0; i < conditions.size(); ++i)
									{
										ImGui::PushID(i);
										auto& condition = conditions[i];
										auto parameter = condition.valueParameter;
										std::string parameterName;
										if (parameter == nullptr)
										{
											parameterName = "NoParameter";
										}
										else
										{
											parameterName = parameter->name;
										}
										auto& compareParameter = condition.CompareParameter;

										if (ImGui::Button(parameterName.c_str(), ImVec2(140, 0)))
										{
											ImGui::OpenPopup("ConditionIndexSelect");
										}
										ImGui::SameLine();
										if (parameter != nullptr)
										{
											if (parameter->vType != ValueType::Trigger)
											{
												if (ImGui::Button(condition.GetConditionType().c_str(), ImVec2(70, 0)))
												{
													ImGui::OpenPopup("ConditionTypeMenu");
												}
											}
											ImGui::SameLine();
											ImGui::SetNextItemWidth(120);
											if (parameter->vType == ValueType::Int)
											{

												ImGui::InputInt("##", &compareParameter.iValue);
											}
											else if (parameter->vType == ValueType::Float)
											{
												ImGui::InputFloat("##", &compareParameter.fValue);
											}
											else if (parameter->vType == ValueType::Bool)
											{
												ImGui::Checkbox("##", &compareParameter.bValue);
											}
											else if (parameter->vType == ValueType::Trigger)
											{
												ImGui::Text("trigger");
											}
										}
										else
										{
											ImGui::Text("No Parmeter", ImVec2(70, 0));
										}
										if (ImGui::BeginPopup("ConditionIndexSelect"))
										{
											for (auto& param : animator->Parameters)
											{
												if (ImGui::MenuItem(param->name.c_str()))
												{
													condition.SetCondition(param->name);
												}
											}
											ImGui::EndPopup();
										}
										if (ImGui::BeginPopup("ConditionTypeMenu"))
										{
											if (parameter->vType == ValueType::Int || parameter->vType == ValueType::Float)
											{
												if (ImGui::MenuItem("Greater"))
													condition.SetConditionType(ConditionType::Greater);
												else if (ImGui::MenuItem("Less"))
													condition.SetConditionType(ConditionType::Less);
												else if (ImGui::MenuItem("Equal"))
													condition.SetConditionType(ConditionType::Equal);
												else if (ImGui::MenuItem("NotEqual"))
													condition.SetConditionType(ConditionType::NotEqual);
											}
											else if (parameter->vType == ValueType::Bool)
											{
												if (ImGui::MenuItem("True"))
													condition.SetConditionType(ConditionType::True);
												else if (ImGui::MenuItem("False"))
													condition.SetConditionType(ConditionType::False);
											}
											ImGui::EndPopup();
										}
										ImGui::SameLine();
										if (ImGui::Button("-"))
										{
											transition->DeleteCondition(i);
										}
										ImGui::PopID();
									}
								}
								if (ImGui::Button("+"))
								{
									if (animator->Parameters.empty())
									{
									}
									else
									{
										auto firstParam = animator->Parameters[0];
										transition->AddConditionDefault(firstParam->name, ConditionType::None, firstParam->vType);
									}
								}
							}
							if (ImGui::Button("Delete Transition All"))
							{
								linkIndex = -1;
								selectedTransitionIndex = -1;
								controller->DeleteTransiton(transition->GetCurState(), transition->GetNextState());

							}
						}
						
					}
					else if (controller != nullptr && controller->m_nodeEditor->m_selectedType == SelectedType::Node && controller->m_nodeEditor->seletedCurNodeIndex != -1)
					{
						nodeEdtior = controller->m_nodeEditor;
						//&&&&& behaviour script 관리할 공간 만들기필요
						if (preInspectorIndex != nodeEdtior->seletedCurNodeIndex)
						{
							selectedTransitionIndex = -1;
						}
						preInspectorIndex = nodeEdtior->seletedCurNodeIndex;
						ImGui::Text("State");
						ImGui::Separator();
						ImGui::PushID(nodeEdtior->seletedCurNodeIndex);
						auto& state = controller->StateVec[nodeEdtior->seletedCurNodeIndex];
						char buffer[128];
						strcpy_s(buffer, state->m_name.c_str());
						buffer[sizeof(buffer) - 1] = '\0';
						ImGui::Text("State Name");
						ImGui::SameLine();
						if (state->m_isAny == false)
						{
							if (ImGui::InputText("##State Name", buffer, sizeof(buffer)))
							{
								nodeEdtior->Nodes[nodeEdtior->seletedCurNodeIndex]->name = buffer;
								for (auto& state : controller->StateVec)
								{
									for (auto& transiton : state->Transitions)
									{
										if (transiton->curStateName == state->m_name)
										{
											transiton->curStateName = buffer;
										}
										if (transiton->nextStateName == state->m_name)
										{
											transiton->nextStateName = buffer;
										}
									}
								}
								state->m_name = buffer;
							}
						}
						else
						{
							ImGui::Text(state->m_name.c_str());
						}
						if (state->m_isAny == false)
						{
							ImGui::Text("Animation Index");
							ImGui::SameLine();
							if (ImGui::InputInt("##Animation Index", &state->AnimationIndex))
							{

							}
							if (ImGui::Button("SetCurState"))
							{
								controller->SetCurState(state->m_name);
							}
						}
						else
						{

						}
						ImGui::Separator();
						ImGui::Text("Transitions");
						if (state->Transitions.empty())
						{
							ImGui::Text("Empty Transiton");
						}
						else
						{
							for (int i = 0; i < state->Transitions.size(); ++i)
							{
								std::string curStateName = state->Transitions[i]->GetCurState();
								std::string nextStateName = state->Transitions[i]->GetNextState();
								std::string transitionName = curStateName + " to " + nextStateName;
								if (ImGui::Selectable(transitionName.c_str(), true))
								{
									selectedTransitionIndex = i;
								}
							}
						}

						if (state->Transitions.size() <= selectedTransitionIndex)
						{
							selectedTransitionIndex = -1;
						}
						if (selectedTransitionIndex != -1)
						{
							
							auto& transition = state->Transitions[selectedTransitionIndex];
							auto& conditions = transition->conditions;
							ImGui::Separator();
							ImGui::Checkbox("HasExitTIme", &transition->hasExitTime);
							ImGui::SliderFloat("ExitTime", &transition->exitTime, 0.1f, 1.0f);
							ImGui::InputFloat("BlendTime", &transition->blendTime);
							ImGui::Separator();
							ImGui::Separator();
							ImGui::Text("Conditions");
							ImGui::Separator();
							if (conditions.empty())
							{
								ImGui::Text("Empty Conditions");
							}
							else
							{
								for (int i = 0; i < conditions.size(); ++i)
								{
									ImGui::PushID(i);
									auto& condition = conditions[i];
									auto parameter = condition.valueParameter;
									std::string parameterName;
									if (parameter == nullptr)
									{
										parameterName = "NoParameter";
									}
									else
									{
										parameterName = parameter->name;
									}
									auto& compareParameter = condition.CompareParameter;

									if (ImGui::Button(parameterName.c_str(), ImVec2(140, 0)))
									{
										ImGui::OpenPopup("ConditionIndexSelect");
									}
									ImGui::SameLine();
									if (parameter != nullptr)
									{
										if (parameter->vType != ValueType::Trigger)
										{
											if (ImGui::Button(condition.GetConditionType().c_str(), ImVec2(70, 0)))
											{
												ImGui::OpenPopup("ConditionTypeMenu");
											}
										}
										ImGui::SameLine();
										ImGui::SetNextItemWidth(120);
										if (parameter->vType == ValueType::Int)
										{

											ImGui::InputInt("##", &compareParameter.iValue);
										}
										else if (parameter->vType == ValueType::Float)
										{
											ImGui::InputFloat("##", &compareParameter.fValue);
										}
										else if (parameter->vType == ValueType::Bool)
										{
											ImGui::Checkbox("##", &compareParameter.bValue);
										}
										else if (parameter->vType == ValueType::Trigger)
										{
											ImGui::Text("trigger");
										}
									}
									else
									{
										ImGui::Text("No Parmeter", ImVec2(70, 0));
									}
									if (ImGui::BeginPopup("ConditionIndexSelect"))
									{
										for (auto& param : animator->Parameters)
										{
											if (ImGui::MenuItem(param->name.c_str()))
											{
												condition.SetCondition(param->name);
											}
										}
										ImGui::EndPopup();
									}
									if (ImGui::BeginPopup("ConditionTypeMenu"))
									{
										if (parameter->vType == ValueType::Int || parameter->vType == ValueType::Float)
										{
											if (ImGui::MenuItem("Greater"))
												condition.SetConditionType(ConditionType::Greater);
											else if (ImGui::MenuItem("Less"))
												condition.SetConditionType(ConditionType::Less);
											else if (ImGui::MenuItem("Equal"))
												condition.SetConditionType(ConditionType::Equal);
											else if (ImGui::MenuItem("NotEqual"))
												condition.SetConditionType(ConditionType::NotEqual);
										}
										else if (parameter->vType == ValueType::Bool)
										{
											if (ImGui::MenuItem("True"))
												condition.SetConditionType(ConditionType::True);
											else if (ImGui::MenuItem("False"))
												condition.SetConditionType(ConditionType::False);
										}
										ImGui::EndPopup();
									}
									ImGui::SameLine();
									if (ImGui::Button("-"))
									{
										transition->DeleteCondition(i);
									}
									ImGui::PopID();
								}
							}
							if (ImGui::Button("+"))
							{
								if (animator->Parameters.empty())
								{
								}
								else
								{
									auto firstParam = animator->Parameters[0];
									transition->AddConditionDefault(firstParam->name, ConditionType::None, firstParam->vType);
								}
							}
						}
						ImGui::PopID();
					}
					ImGui::EndChild();
				ImGui::End();
			}		



		}
	}
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
		const char* modes[] = { "Raise", "Lower", "Flatten", "PaintLayer" };
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
			for (const auto& mask : g_CurrentBrush->m_masks) {
				if (ImGui::ImageButton(maskNames[maskIndex], (ImTextureID)mask.m_maskSRV, ImVec2((float)100.0f, (float)100.0f))) {
					if (selectedMaskIndex != maskIndex) {
						selectedMaskIndex = maskIndex;
						uint32_t id = static_cast<uint32_t>(maskIndex);
						g_CurrentBrush->SetMaskID(maskIndex); // 선택된 마스크 ID 설정
					}
					else {
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
		if (ImGui::Button("mask texture load")) {
			file::path maskTexture = ShowOpenFileDialog(L"");
			terrainComponent->SetBrushMaskTexture(g_CurrentBrush, maskTexture);
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
