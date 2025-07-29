#include "ReflectionImGuiHelper.h"
#include "Animation.h"
#include "Animator.h"
#include "Skeleton.h"
#include "NodeEditor.h"
#include "AnimationController.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ExternUI.h"

void ImGuiDrawHelperAnimator(Animator* animator)
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
			static int selectedControllerIndex = -1;
			static int preSelectIndex = -1;
			static int linkIndex = -1;
			static int ClickNodeIndex = -1;
			static int targetNodeIndex = -1;
			static int selectedTransitionIndex = -1;
			static int preInspectorIndex = -1; //인스펙터에뛰운 인덱스번호 
			static int AvatarControllerIndex = -1;
			static bool showAvatarMaskWindow = false;
			if (ImGui::Button(ICON_FA_BOX))
			{
				showControllersWindow = !showControllersWindow;
			}
			if (showControllersWindow)
			{





				bool open = ImGui::Begin("Animation Controllers", &showControllersWindow);
				//int i = 0;

				if (open && ImGui::IsWindowAppearing())
				{
					selectedControllerIndex = -1;
					preSelectIndex = -1;
					linkIndex = -1;
					ClickNodeIndex = -1;
					targetNodeIndex = -1;
					selectedTransitionIndex = -1;
					preInspectorIndex = -1; //인스펙터에뛰운 인덱스번호 
					AvatarControllerIndex = -1;
					showAvatarMaskWindow = false;
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

							if (ImGui::Selectable(controller->name.c_str(), true, 0, ImVec2(150, 0)))
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
									preSelectIndex = -1;
									linkIndex = -1;
									ClickNodeIndex = -1;
									targetNodeIndex = -1;
									selectedTransitionIndex = -1;
									preInspectorIndex = -1; //인스펙터에뛰운 인덱스번호 
									AvatarControllerIndex = -1;
									showAvatarMaskWindow = false;
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
							if (ImGui::InputText("", buffer, sizeof(buffer)))
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
				ImGui::BeginChild("Controller Info", ImVec2(900, 500), false);
				if (!animator->m_animationControllers.empty() && selectedControllerIndex != -1)
					controller = animator->m_animationControllers[selectedControllerIndex].get();
				std::string controllerName;
				if (controller)
				{
					controllerName = controller->name + " Controller Info";
				}
				else
				{
					controllerName = " Controller Info";
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
				ImGui::BeginChild("Inspector Info", ImVec2(1300, 500), false);
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

					if (!transitions.empty())
					{
						for (int i = 0; i < transitions.size(); ++i)
						{
							auto& transition = transitions[i];
							std::string curStateName = transition->GetCurState();
							std::string nextStateName = transition->GetNextState();
							std::string transitionName = curStateName + " to " + nextStateName;

							bool isSelected = (selectedTransitionIndex == i);
							if (ImGui::Selectable(transitions[i]->m_name.c_str(), isSelected))
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
										std::string parameterName = condition.valueName;
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