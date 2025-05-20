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

#include "IconsFontAwesome6.h"
#include "fa.h"
#include "imgui-node-editor/imgui_node_editor.h"
constexpr XMVECTOR FORWARD = XMVECTOR{ 0.f, 0.f, 1.f, 0.f };
constexpr XMVECTOR UP = XMVECTOR{ 0.f, 1.f, 0.f, 0.f };

InspectorWindow::InspectorWindow(SceneRenderer* ptr) :
	m_sceneRenderer(ptr)
{
	ImGui::ContextRegister("Inspector", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		Scene* scene = nullptr;
		RenderScene* renderScene = nullptr;
		GameObject* selectedSceneObject = nullptr;

		if (m_sceneRenderer)
		{
			scene = SceneManagers->GetActiveScene();
			renderScene = m_sceneRenderer->m_renderScene;
			selectedSceneObject = renderScene->m_selectedSceneObject;

			if (!scene && !renderScene)
			{
				ImGui::Text("Not Init InspectorWindow");
				ImGui::End();
				return;
			}
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
					selectedSceneObject->m_transform.GetLocalMatrix();
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
				//if (ImGui::CollapsingHeader("Controllers"))
				{
					int i = 0;
					static int selectedControllerIndex = 0;
					ImGui::BeginChild("Controllers", ImVec2(200, 500), true); // 고정 너비, 스크롤 가능
					ImGui::Text("Controllers");
					ImGui::Separator();

					auto& controllers = animator->m_animationControllers;
					for(int index = 0; index < controllers.size(); ++index)
					{
						auto& controller = controllers[index];
						bool isSelected = (selectedControllerIndex == index);
						ImGui::PushID(index);
						
						float selectableWidth = ImGui::GetContentRegionAvail().x - 33.0f; // 버튼 폭만큼 빼기
					
						if (ImGui::Selectable(controller->name.c_str(), isSelected, 0, ImVec2(selectableWidth, 0)))
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

							const auto& mat_controller_type = Meta::Find("AnimationController");
							Meta::DrawProperties(controller, *mat_controller_type);
							Meta::DrawMethods(controller, *mat_controller_type);

							ImGui::EndPopup();
						}
						ImGui::PopID();
					}
					ImGui::EndChild();

					ImGui::SameLine();
					ImGui::BeginChild("Controller Info", ImVec2(0, 500), true); 
					ImGui::Text("Controller Info");
					ImGui::Separator();
					if (selectedControllerIndex >= 0 && selectedControllerIndex < animator->m_animationControllers.size())
					{
						auto& controller = animator->m_animationControllers[selectedControllerIndex];
						std::string fileName = controller->name + ".node_editor.json";
						if (!controller->StateVec.empty())
						{
							namespace ed = ax::NodeEditor;
							if (!controller->conEdit)
							{
								ed::Config config;
								config.SettingsFile = fileName.c_str();
								controller->conEdit = ed::CreateEditor(&config);
							}

							ed::SetCurrentEditor(controller->conEdit);
							ed::Begin("State Machine Editor");
							for (auto& state : controller->StateVec)
							{
								
								ed::NodeId nodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(state->m_name));
								ed::BeginNode(nodeID);
								ImGui::Text(state->m_name.c_str());
								ed::PinId inputPinID = ed::PinId((static_cast<uint64_t>(nodeID) << 1) | 0);
								ed::PinId outputPinID = ed::PinId((static_cast<uint64_t>(nodeID) << 1) | 1);
								ed::BeginPin(inputPinID, ed::PinKind::Input);
								ImGui::Dummy(ImVec2(1, 1));
								ed::EndPin();

								ed::BeginPin(outputPinID, ed::PinKind::Output);
								ImGui::Dummy(ImVec2(1, 1));
								ed::EndPin();

								ed::EndNode();

								for(auto& trans : state->Transitions)
								{
									ed::NodeId nodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(trans->GetCurState()));
									ed::NodeId nextnodeID = static_cast<ed::NodeId>(std::hash<std::string>{}(trans->GetNextState()));
									
									ed::PinId startPin = ed::PinId((static_cast<uint64_t>(nodeID) << 1) | 1);
									ed::PinId endPin = ed::PinId((static_cast<uint64_t>(nextnodeID) << 1) | 0);
									ed::LinkId linkId = static_cast<ed::LinkId>(std::hash<std::string>{}(trans->m_name));
									ed::Link(linkId, startPin,endPin);
								}
							}

							ed::End();
							ed::SetCurrentEditor(nullptr);
							if (ImGui::CollapsingHeader("States"))
							{
								const auto& mat_animationState_type = Meta::Find("AnimationState");
								for (auto& shardAnimationState : controller->StateVec)
								{
									AnimationState* AnimationState = shardAnimationState.get();
									ImGui::PushID(AnimationState->m_name.c_str());
									if (ImGui::CollapsingHeader(AnimationState->m_name.c_str()))
									{
										Meta::DrawProperties(AnimationState, *mat_animationState_type);
										//Meta::DrawMethods(AnimationState, *mat_controller_type);
									}
									ImGui::PopID();
								}

							}
						}
					}

					ImGui::EndChild();
				}
				ImGui::End(); // 창 닫기
			}			
		}
	}
}