#include "EditorObjectOperations.h"
#include "ReflectionImGuiHelper.h"
#include "ReflectionTypedDraw.h"
#include "ClrHost.h"
#include "Animator.h"
#include "NodeEditor.h"
#include "AnimationController.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "ExternUI.h"
// DataSystems가 여기 있다. 유니티 빌드에서는 앞선 파일이 공급했다.
#include "DataSystem.h"

// 컨트롤러별 노드 편집기 세션 — 에디터가 소유한다(E3-5). Core의
// AnimationController는 편집기 세션을 모른다. NodeEditor의 프레임 간 상태는
// SettingsFile 경로에 결속된 컨텍스트뿐이고 MakeEdit이 경로 변화 시
// 재생성하므로, 슬롯 재사용이 이월시키는 상태는 없다.
static NodeEditor* GetControllerNodeEditor(AnimationController* controller)
{
	static std::unordered_map<AnimationController*, std::unique_ptr<NodeEditor>> s_controllerNodeEditors;
	auto& slot = s_controllerNodeEditors[controller];
	if (!slot)
	{
		slot = std::make_unique<NodeEditor>();
	}
	return slot.get();
}

void ImGuiDrawHelperAnimator(Animator* animator)
{
	if (animator)
	{
		static bool showControllersWindow = false;
		static bool showKeyFrameWindow = false;
		static int  animationIndex = 0;
		const auto& aniType = Meta::Find(animator->GetTypeID());
		Meta::TypedDraw::DrawOwnMembers(*animator);
		Meta::DrawMethods(animator, *aniType);
		if (ImGui::CollapsingHeader("animations"))
		{
			// I5-D5b — 열거·이름도 창구를 지난다. D4e-2가 편집을 Animator
			// 소유로 옮겼으나 목록은 공유 자산을 직접 훑고 있었다 — 인덱스
			// 축이 두 출처로 갈리면 편집 정본과 표시 대상이 어긋난다.
			const int clipCount = static_cast<int>(animator->GetClipCount());
			for (int i = 0; i < clipCount; ++i)
			{
				const std::string clipName = animator->GetClipName(i);
				ImGui::PushID(clipName.c_str());
				ImGui::Text("%s", clipName.c_str());
				ImGui::Text("Loop");
				ImGui::SameLine();
				// I5-D4e-2 — 루프·이벤트 편집의 정본은 Animator 클립
				// 오버라이드다. 구 코드는 공유 자산(m_Skeleton->m_animations)을
				// 직접 편집해 같은 스켈레톤을 공유하는 다른 Animator까지
				// 바뀌었다 — 발화·저장 정본과 편집 표면을 함께 옮긴다.
				bool looping = animator->IsClipLooping(i);
				if (ImGui::Checkbox("", &looping))
				{
					animator->SetClipLooping(i, looping);
				}
				ImGui::Text("KeyFrameEvent");
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_BOX))
				{
					ImGui::PopID();
					animationIndex = i;
					showKeyFrameWindow = !showKeyFrameWindow;
				}
				else
				{
					ImGui::PopID();
				}
				ImGui::Separator();
			}
			ImGui::Separator();
			if (showKeyFrameWindow)
			{
				ImGui::SetNextWindowSize(ImVec2(1100, 400), ImGuiCond_FirstUseEver);
				bool open = ImGui::Begin("Event", &showKeyFrameWindow);
				ImGui::Text("%s", animator->GetClipName(animationIndex).c_str());
				if (ImGui::Button("Add Event"))
				{
					animator->AddClipEvent(animationIndex);
				}
				ImGui::Separator();
				ImGui::Separator();
				AnimatorClipOverride* clipOverride =
					animator->FindClipOverride(animationIndex);
				if (clipOverride && !clipOverride->events.empty())
				{
					bool deletedThisFrame = false;
					int eventIndex = 0;
					for (auto& event : clipOverride->events)
					{
						ImGui::PushID(eventIndex);
						ImGui::Dummy(ImVec2(10.0f, 0));
						ImGui::SameLine();
						ImGui::Text("eventName");
						ImGui::SameLine();
						char eventBuffer[128];
						strcpy_s(eventBuffer, event.m_eventName.c_str());
						eventBuffer[sizeof(eventBuffer) - 1] = '\0';
						ImGui::SetNextItemWidth(150);
						if (ImGui::InputText("##event", eventBuffer, sizeof(eventBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							event.m_eventName = eventBuffer;
						}
						ImGui::SameLine();
						ImGui::Dummy(ImVec2(10.0f, 0));
						ImGui::SameLine();
						ImGui::Text("scriptName");
						ImGui::SameLine();

						char scriptBuffer[128];
						strcpy_s(scriptBuffer, event.m_scriptName.c_str());
						scriptBuffer[sizeof(scriptBuffer) - 1] = '\0';
						ImGui::SetNextItemWidth(150);
						if (ImGui::InputText("##script", scriptBuffer, sizeof(scriptBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							event.m_scriptName = scriptBuffer;
						}
							

						ImGui::SameLine();
						ImGui::Dummy(ImVec2(10.0f, 0));
						ImGui::SameLine();
						ImGui::Text("funName");
						ImGui::SameLine();


						char funBuffer[128];
						strcpy_s(funBuffer, event.m_funName.c_str());
						funBuffer[sizeof(funBuffer) - 1] = '\0';
						ImGui::SetNextItemWidth(150);
						if (ImGui::InputText("##fun", funBuffer, sizeof(funBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
						{
							event.m_funName = funBuffer;
						}
						ImGui::SameLine();
						ImGui::Dummy(ImVec2(10.0f, 0));
						ImGui::SameLine();
						ImGui::Text("Frame Key");
						ImGui::SameLine();
						ImGui::PushItemWidth(80);



						if (ImGui::InputInt("##frame key", &event.frameKey, 0, 0))
						{
							// I5-D5b — 키프레임 수도 창구를 지난다. 역브리지가
							// 이 값을 "채널 키 개수의 합"으로 채우고 있어
							// legacy 임포터 정의(유니크 키 시각 수)와 한
							// 자릿수씩 갈렸다 — 상한과 key(0~1 진행률) 환산이
							// 로드 경로마다 달라지는 실결함이었다.
							const int totalKeyFrames = static_cast<int>(
								animator->GetClipFrameCount(animationIndex));
							if (event.frameKey < 1) event.frameKey = 1;
							if (totalKeyFrames > 0)
							{
								if (event.frameKey > totalKeyFrames)
									event.frameKey = totalKeyFrames;
								event.key = float(event.frameKey)
									/ float(totalKeyFrames);
							}
						}

						//ImGui::DragFloat(("Key##" + event.m_eventName).c_str(), &event.key, 0.01f,0.0f, 1.0f);
						ImGui::PopItemWidth();
						ImGui::SameLine();
						if(ImGui::Button("delete"))
						{
							// 순회 중 erase는 참조 무효화 — 프레임당 하나만
							// 지우고 즉시 순회를 끝낸다(구 코드의 잠재 UB 교정).
							animator->DeleteClipEvent(animationIndex, eventIndex);
							deletedThisFrame = true;
						}
						ImGui::Separator();
						eventIndex++;
						ImGui::PopID();
						if (deletedThisFrame) break;
					}

				}
				ImGui::End();
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
			static bool isOpenAniBehaviorPopup = false;
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
									GetControllerNodeEditor(controller.get())->ReNameJson(filename);
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
								EditorObjectOperations::AnimatorDefaultParameter(*animator, ValueType::Float);
							}
							if (ImGui::MenuItem("Add Int"))
							{
								EditorObjectOperations::AnimatorDefaultParameter(*animator, ValueType::Int);
							}
							if (ImGui::MenuItem("Add Bool"))
							{
								EditorObjectOperations::AnimatorDefaultParameter(*animator, ValueType::Bool);
							}
							if (ImGui::MenuItem("Add Trigger"))
							{
								EditorObjectOperations::AnimatorDefaultParameter(*animator, ValueType::Trigger);
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
						GetControllerNodeEditor(controller)->MakeEdit(fileName);

						for (auto& state : controller->StateVec)
						{
							GetControllerNodeEditor(controller)->MakeNode(state->m_name);
						}

						for (auto& state : controller->StateVec)
						{
							for (auto& trans : state->Transitions)
							{
								GetControllerNodeEditor(controller)->MakeLink(trans->GetCurState(), trans->GetNextState(), trans->m_name);
							}
						}

						GetControllerNodeEditor(controller)->DrawLink(&linkIndex);
						GetControllerNodeEditor(controller)->DrawNode(&ClickNodeIndex);
						GetControllerNodeEditor(controller)->Update();

						if (targetNodeIndex != -1)
						{
							auto states = controller->StateVec;
							int curIndex = GetControllerNodeEditor(controller)->seletedCurNodeIndex;
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
						GetControllerNodeEditor(controller)->EndEdit();
						if (isOpenNodePopUp)
						{
							ImGui::OpenPopup("NodeMenu");
						}
						if (ImGui::BeginPopup("NodeMenu"))
						{
							if (ImGui::MenuItem("Make Transition"))
							{
								GetControllerNodeEditor(controller)->MakeNewLink(&targetNodeIndex);
								isOpenNodePopUp = false;
								ClickNodeIndex = -1;
							}
							if (ImGui::MenuItem("Delete State"))
							{
								controller->DeleteState(controller->StateVec[ClickNodeIndex]->m_name);
								isOpenNodePopUp = false;

								if (ClickNodeIndex == GetControllerNodeEditor(controller)->seletedCurNodeIndex)
								{
									GetControllerNodeEditor(controller)->seletedCurNodeIndex = -1;
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
				ImGui::BeginChild("Inspector Info", ImVec2(400, 500), false);
				ImGui::Text("Inspector");
				ImGui::Separator();
				static AnimationState* selectedState = nullptr;
				if (preSelectIndex != selectedControllerIndex)
				{
					linkIndex = -1;
					ClickNodeIndex = -1;
					targetNodeIndex = -1;
					preSelectIndex = selectedControllerIndex;

				}
				if (controller != nullptr && GetControllerNodeEditor(controller)->m_selectedType == SelectedType::Link && linkIndex != -1)
				{
					if (preInspectorIndex != linkIndex)
					{
						selectedTransitionIndex = -1;
					}
					preInspectorIndex = linkIndex;
					ImGui::Text("Transitions");
					ImGui::Separator();
					std::string fromNode = GetControllerNodeEditor(controller)->Links[linkIndex]->fromNode->name;
					std::string toNode = GetControllerNodeEditor(controller)->Links[linkIndex]->toNode->name;
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
				else if (controller != nullptr && GetControllerNodeEditor(controller)->m_selectedType == SelectedType::Node && GetControllerNodeEditor(controller)->seletedCurNodeIndex != -1)
				{
					nodeEdtior = GetControllerNodeEditor(controller);
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
						
	
						if (ImGui::InputText("##stateName", buffer, sizeof(buffer)))
						{
							if (ImGui::IsItemDeactivatedAfterEdit())
							{
								// 실제 변경 적용은 입력 완료 시점에만 수행
								nodeEdtior->Nodes[nodeEdtior->seletedCurNodeIndex]->name = buffer;

								for (auto& st : controller->StateVec)
								{
									for (auto& transiton : st->Transitions)
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

						ImGui::Text("Animation Speed");
						ImGui::SameLine();
						if (ImGui::InputFloat("##AnimationSpeed", &state->animationSpeed))
						{

						}
						ImGui::Text("MultiplierAnimation Speed");
						ImGui::SameLine();
						
						if (ImGui::Button(state->animationSpeedParameterName.c_str(), ImVec2(140, 0)))
						{
							if (state->useMultipler)
							{
								ImGui::OpenPopup("animationSpeedParameterSelecet");
							}
						}
						ImGui::SameLine();

						if (ImGui::Checkbox("Parameter", &state->useMultipler))
						{

						}
						if (ImGui::Button("SetCurState"))
						{
							controller->SetCurState(state->m_name);
						}

						if (ImGui::BeginPopup("animationSpeedParameterSelecet"))
						{
							for (auto& param : animator->Parameters)
							{
								if (param->vType != ValueType::Float) continue;
								if (ImGui::MenuItem(param->name.c_str()))
								{
									state->animationSpeedParameterName = param->name;
								}
							}
							ImGui::EndPopup();
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
					const float buttonWidth = 210.0f; // 버튼의 가로 너비 (임의로 지정 또는 측정)
					const float windowWidth = ImGui::GetContentRegionAvail().x;
					float offsetX = (windowWidth - buttonWidth) * 0.5f;
					std::string stateName{};
					ImGui::Separator();
					ImGui::Text("Animation Behaviour");
					if (nullptr == state->behaviour)
					{
						stateName = "None Animation Behaviour";
					}
					else
					{
						stateName = state->behaviour->m_name;
					}
					
					if (ImGui::Button(stateName.c_str(), ImVec2(buttonWidth, 0)))
					{
						if(nullptr != state->behaviour)	
						{
							FileGuid fileGuid = DataSystems->GetStemToGuid(state->behaviour->m_name);
							file::path scriptFullPath = DataSystems->GetFilePath(fileGuid);
							if (scriptFullPath.empty())
							{
								Debug->LogError("Script not found: " + state->behaviour->m_name);
							}
							else
							{
								// C++ 스크립트 솔루션은 은퇴(9-4) — 파일 위치만 알려 준다.
								Debug->Log("Behaviour script: " + scriptFullPath.string());
							}
						}
					}
					
					ImGui::SameLine();
					if (ImGui::Button("Delete behavior"))
					{
						state->ClearBehaviour();
					}
					// 커서 위치 이동
					if (offsetX > 0.0f)
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

					if (ImGui::Button("Add Behaviour", ImVec2(buttonWidth, 0)))
					{
						selectedState = state.get();
						isOpenAniBehaviorPopup = true;
					}

					ImGui::PopID();
				}
				ImGui::EndChild();

				if (isOpenAniBehaviorPopup)
				{
					ImGui::OpenPopup("AniBehaviorSelect");
					isOpenAniBehaviorPopup = false;
				}

				static ImGuiTextFilter searchFilter;

				ImGui::SetNextWindowSize(ImVec2(350, 0)); // 원하는 사이즈 지정
				if(ImGui::BeginPopup("AniBehaviorSelect"))
				{
					ImGui::Text("Add AniBehavior");
					ImGui::Separator();

					float availableWidth = ImGui::GetContentRegionAvail().x;
					searchFilter.Draw(ICON_FA_MARKER "Search", availableWidth);

					// C# 애니메이션 상태 스크립트 목록. 등록된 타입 이름은 ClrHost가 내준다
					// (구 C++ 팩토리 목록을 대체한다 — 9-4).
					const auto aniTypeNames = ClrHost::Get().GetAniBehaviourTypeNames();
					if (aniTypeNames.empty())
					{
						ImGui::TextDisabled(ClrHost::Get().IsReady()
							? "등록된 AniBehaviour가 없습니다"
							: "CLR이 준비되지 않았습니다");
					}

					for (const auto& typeName : aniTypeNames)
					{
						if (!searchFilter.PassFilter(typeName.c_str()))
							continue;

						if (ImGui::Selectable(typeName.c_str()) && selectedState)
						{
							selectedState->SetBehaviour(typeName);
							ImGui::CloseCurrentPopup();
						}
					}

					ImGui::EndPopup();
				}



				ImGui::End();
			}
		}
	}
}
