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

#include "IconsFontAwesome6.h"
#include "fa.h"

#include "NodeEditor.h"
namespace ed = ax::NodeEditor;
static const std::unordered_set<std::string> ignoredKeys = {
	"guid",
	"importSettings"
};

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
	ImGui::ContextRegister("Inspector", [&]()
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
			renderScene = m_sceneRenderer->m_renderScene;
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
		else if (metaNodeJustSelected)
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

			if (ImGui::InputText("name",
				&name[0],
				name.capacity() + 1,
				ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
				Meta::InputTextCallback,
				static_cast<void*>(&name)))
			{
				selectedSceneObject->m_name.SetString(name);
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
				for (float& i : pyr) i *= Mathf::Rad2Deg;

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
					Mathf::Vector3 radianEuler(pyr[0] * Mathf::Deg2Rad, pyr[1] * Mathf::Deg2Rad, pyr[2] * Mathf::Deg2Rad);
					rotation = XMQuaternionRotationRollPitchYaw(radianEuler.x, radianEuler.y, radianEuler.z);
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
					if(isOpen)
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
					selectedSceneObject->RemoveComponent(selectedComponent);
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

void InspectorWindow::ImGuiDrawHelperMeshRenderer(MeshRenderer* meshRenderer)
{
	if (meshRenderer->m_Material)
	{
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Element ");
			ImGui::SameLine();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
			ImGui::Button(meshRenderer->m_Material->m_name.c_str(), ImVec2(250, 0));
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_BOX))
			{
				ImGui::GetContext("SelectMatarial").Open();
			}
			ImGui::PopStyleVar(2);
		}
		if (ImGui::CollapsingHeader("MaterialInfo", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto& mat_type = Meta::Find("Material");
			const auto& mat_info_type = Meta::Find("MaterialInfomation");
			Meta::DrawProperties(&meshRenderer->m_Material->m_materialInfo, *mat_info_type);
			const auto& mat_render_type = Meta::FindEnum("MaterialRenderingMode");
			for (auto& enumProp : mat_type->properties)
			{
				if (enumProp.typeID == TypeTrait::GUIDCreator::GetTypeID<MaterialRenderingMode>())
				{
					Meta::DrawEnumProperty((int*)&meshRenderer->m_Material->m_renderingMode, mat_render_type, enumProp);
					break;
				}
			}
		}
		if (ImGui::CollapsingHeader("LightMapping", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto& lightmap_type = Meta::Find("LightMapping");
			Meta::DrawProperties(&meshRenderer->m_LightMapping, *lightmap_type);
		}

		if (DataSystems->m_trasfarMaterial)
		{
			std::string name = meshRenderer->m_Material->m_name;
			Meta::MakeCustomChangeCommand(
			[=]
			{
				meshRenderer->m_Material = DataSystems->Materials[name].get();
			},
			[=]
			{
				meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
			});

			meshRenderer->m_Material = DataSystems->m_trasfarMaterial;
			DataSystems->m_trasfarMaterial = nullptr;
		}
	}
	else
	{
		ImGui::Text("No Material");
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

		if (!animator->m_animationControllers.empty())
		{
			ImGui::Text("Controllers ");
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_BOX))
			{
				showControllersWindow = !showControllersWindow;
			}
			if(showControllersWindow)
			{
				ImGui::Begin("Animation Controllers", &showControllersWindow);
				int i = 0;
				static int selectedControllerIndex = 0;
				static int preSelectIndex = 0;
				static int linkIndex = -1;
				static int ClickNodeIndex = -1;
				static int targetNodeIndex = -1;
				static int selectedTransitionIndex = -1;
				static int preInspectorIndex = -1; //인스펙터에뛰운 인덱스번호 
				static int AvatarControllerIndex = -1;
				static bool showAvatarMaskWindow = false;
				auto& controllers = animator->m_animationControllers;
				ImGui::BeginChild("Leftpanel", ImVec2(200, 500), true); 
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
								selectedControllerIndex = index;
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
									controller->m_nodeEditor->ReNameJson(buffer);
								}

								ImGui::Text("Avatar Mask"); 
								ImGui::SameLine();
								if (controller->useMask)
								{
									if (ImGui::SmallButton(ICON_FA_PUZZLE_PIECE))
									{
										AvatarControllerIndex = i;
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
									drawMaskTree(rootMask); // depth 0에서 시작
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
					ImGui::EndChild();
					ImGui::SameLine();
					ImGui::BeginChild("Controller Info", ImVec2(1200, 500), true); 
					ImGui::Text("Controller Info");
					ImGui::Separator();
					auto& controller = animator->m_animationControllers[selectedControllerIndex];
					auto& nodeEdtior = controller->m_nodeEditor;
					if (selectedControllerIndex >= 0 && selectedControllerIndex < animator->m_animationControllers.size())
					{
						//auto& controller = animator->m_animationControllers[selectedControllerIndex];
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
						std::string fileName = controller->name + ".node_editor.json";
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
								controller->CreateTransition(states[curIndex]->m_name, states[targetNodeIndex]->m_name);
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
					ImGui::BeginChild("Inspector Info", ImVec2(0, 500), true);
					ImGui::Text("Inspector");
					ImGui::Separator();
					if (controller->m_nodeEditor->m_selectedType == SelectedType::Link && linkIndex != -1)
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

						for (auto& transiton : transitions)
						{
							std::string curStateName = transiton->GetCurState();
							std::string nextStateName = transiton->GetNextState();
							std::string transitionName = curStateName + " to " + nextStateName;
							if (ImGui::Selectable(transitionName.c_str(), true))
							{
								selectedTransitionIndex = i;
							}

							if (selectedTransitionIndex != -1)
							{
								ImGui::Separator();
								ImGui::Separator();
								ImGui::Text("Conditions");
								ImGui::Separator();
								auto& conditions = transiton->conditions;
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
										}
										else
										{
											ImGui::Text("No Parmeter", ImVec2(70, 0));
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
											transiton->DeleteCondition(i);
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
										transiton->AddConditionDefault(firstParam->name, ConditionType::None, firstParam->vType);
									}
								}
							}
							if (ImGui::Button("Delete Transition All"))
							{
								linkIndex = -1;
								controller->DeleteTransiton(transiton->GetCurState(), transiton->GetNextState());
							}
						}
						//&&&&& 전이조건 condition 뛰우기 해당전이가 여러개면 
					}
					else if (nodeEdtior->m_selectedType == SelectedType::Node && nodeEdtior->seletedCurNodeIndex != -1)
					{
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
						if (ImGui::InputText("##State Name", buffer, sizeof(buffer)))
						{
							state->m_name = buffer;
							nodeEdtior->Nodes[nodeEdtior->seletedCurNodeIndex]->name = buffer;
						}
						ImGui::Text("Animation Index");
						ImGui::SameLine();
						if (ImGui::InputInt("##Animation Index", &state->AnimationIndex))
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
						if (selectedTransitionIndex != -1)
						{
							ImGui::Separator();
							ImGui::Separator();
							ImGui::Text("Conditions");
							ImGui::Separator();
							auto& transition = state->Transitions[selectedTransitionIndex];
							auto& conditions = transition->conditions;
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
									}
									else
									{
										ImGui::Text("No Parmeter", ImVec2(70, 0));
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

	int prewidth = terrainComponent->m_width;
	int preheight = terrainComponent->m_height;
	int editWidth = terrainComponent->m_width;
	int editHeight = terrainComponent->m_height;

	
	//bool change_edit = ImGui::IsItemDeactivatedAfterEdit();
	
	if (ImGui::InputInt("Width", &editWidth)) {
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if(editWidth < 1)
			{
				editWidth = 1;
			}
		}
	}
	if (ImGui::InputInt("Height", &editHeight)) {
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (editHeight < 1)
			{
				editHeight = 1;
			}
		}
	}	

	if (prewidth != editWidth || preheight != editHeight)
	{
		terrainComponent->Resize(editWidth, editHeight);
	}


	// ImGui UI 예시 (에디터 툴 패널 내부)
	if (ImGui::CollapsingHeader("Terrain Brush"))
	{
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
			ImGui::SliderFloat("Strength", &g_CurrentBrush->m_flatTargetHeight, -100.0f, 500.0f);
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
				ImGui::OpenPopup("AddLayerPopup");
			}
		}

		//save , load
		if (ImGui::Button("Save Terrain"))
		{
			
			file::path savePath = ShowSaveFileDialog(L"", L"Save File",PathFinder::Relative("Terrain"));
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

	// 레이어 추가 팝업
	if (ImGui::BeginPopup("AddLayerPopup"))
	{
		if (ImGui::Button("Add"))
		{
			file::path difuseFile = ShowOpenFileDialog(L"");
			std::wstring difuseFileName = difuseFile.filename();
			if (!difuseFile.empty())
			{
				terrainComponent->AddLayer(difuseFile, difuseFileName, 10.0f);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

