#include "InspectorWindow.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "EditorImGuiTexture.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
#include "Entity.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "ICustomEditor.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "DataSystem.h"
#include "EditorAssetDatabase.h"
#include "ContentsBrowserWindow.h"
#include "EditorSessionState.h"
#include "PathFinder.h"
#include "RuntimeSettings.h"
#include "Transform.h"
#include "ComponentFactory.h"
#include "ReflectionImGuiHelper.h"
#include "ReflectionTypedDraw.h"   // CT6-c typed Draw 썽크
#include "RegisterReflectManual.h" // REFLECT_TYPE_LIST 공유 목록 + 전 타입 헤더
#include "CustomCollapsingHeader.h"
#include "Terrain.h"
#include "FileDialog.h"
#include "TagManager.h"
#include "PlayerInput.h"
#include "InputActionManager.h"
#include "SoundManager.h"
#include <mathematics/scalar.hpp>
//----------------------------
#include "ExternUI.h"
#include "StateMachineComponent.h"
#include "BehaviorTreeComponent.h"
#include "FoliageComponent.h"
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

// C# 스크립트 부착: ScriptComponent(다중 허용 경로)를 붙이고 타입을 지정한 뒤
// 정상 드레인 경로(Scene::Awake)를 동기로 한 번 태워 관리 인스턴스를 만든다.
//
// (C2-2) 예전에는 여기서 script->OnInitialized()를 직접 불렀다. 하지만
// AddComponentAllowMultiple 안의 AttachComponentLifecycle이 이미 이 컴포넌트를
// PendingAwake 큐에 넣어 뒀고(State_AwakeCalled 비트는 아직 서지 않은 채), 직접
// 부르면 그 비트를 세우지 않으므로 다음 프레임 Scene::RegistryDrainAwakeAndStart가
// 큐에 남은 같은 컴포넌트를 또 한 번 깨운다 — OnInitialized 이중 호출.
// ScriptComponent::OnInitialized의 `if (HasInstance()) return;` 가드가 보통은
// 이걸 조용히 삼키지만, 그건 설계가 아니라 우연이다(ScriptComponent.cpp).
//
// Api_Prefab_Instantiate(ClrHost.cpp)가 쓰는 것과 같은 관용구로 고친다 — 부착
// 직후 scene->DrainPendingLifecycle()을 동기로 불러 정상 드레인 경로를 태운다. 이미 깨운
// 컴포넌트는 State_AwakeCalled로 건너뛰므로 씬 전체를 다시 돌아도 안전하다.
static void AttachManagedScript(Entity* obj, const std::string& typeName)
{
	const Meta::Type* scriptType = Meta::Find(type_guid(ScriptComponent));
	if (nullptr == obj || nullptr == scriptType) return;

	// K2 스테이지 A: AddComponentAllowMultiple가 raw Component*를 돌려준다.
	auto* component = obj->AddComponentAllowMultiple(*scriptType);
	auto* script = dynamic_cast<ScriptComponent*>(component);
	if (nullptr == script)
	{
		Debug->LogError("[스크립트] ScriptComponent 생성 실패");
		return;
	}

	// m_scriptType은 드레인보다 먼저 세워야 한다 — OnInitialized가 이 값을 보고
	// CreateBehaviour를 부른다(비어 있으면 그냥 돌아간다. ScriptComponent.cpp).
	script->m_scriptType = typeName;

	if (Scene* scene = obj->GetScene())
	{
		scene->DrainPendingLifecycle();
	}
}

ed::EditorContext* m_fsmEditorContext{ nullptr };
bool			   s_CreatingLink = false;
ed::PinId		   s_LinkStartPin = 0;
ed::LinkId		   s_EditLinkId = 0;
bool			   s_RenameNodePopup{ false };

ed::EditorContext* s_BTEditorContext{ nullptr };

// CT6-c: typed Draw 등록 — 전 타입의 위젯 트리 인스턴스화를 이 TU 한 곳에
// 가둔다. 목록은 등록 정본(RegisterReflectManual.h)의 X-매크로를 공유한다.
static void RegisterAllTypedDraws()
{
#define REFLECT_DRAW_ONE(T) Meta::TypedDraw::RegisterDraw<T>();
	REFLECT_TYPE_LIST(REFLECT_DRAW_ONE)
#undef REFLECT_DRAW_ONE
}

InspectorWindow::InspectorWindow()
{
	RegisterAllTypedDraws();

	ImGui::ContextRegister(ICON_FA_CIRCLE_INFO "  Inspector", [&]()
	{
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());

		static Entity* prevSelectedSceneObject = nullptr;
		static bool wasMetaSelectedLastFrame = false;

		Scene* scene = nullptr;
		RenderScene* renderScene = nullptr;
		Entity* selectedSceneObject = nullptr;
		std::optional<MetaYml::Node>& selectedNode{ ContentsBrowserWindow::selectedFileMetaNode };
		bool isSelectedNode = selectedNode.has_value();
		file::path selectedFileName{ ContentsBrowserWindow::selectedFileName };
		file::path selectedMetaFilePath{ ContentsBrowserWindow::selectedMetaFilePath };

		if (SceneManagers->IsSceneLoading())
		{
			ImGui::Text("Not Init InspectorWindow");
			//ImGui::End();
			return;
		}

		scene = SceneManagers->GetActiveScene();
		renderScene = SceneManagers->GetRenderScene();
		if (scene && renderScene)
		{
			selectedSceneObject = scene->m_selectedEntity;

			if (!scene && !renderScene)
			{
				ImGui::Text("Not Init InspectorWindow");
				//ImGui::End();
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

		TerrainBrush* terrainBrush = EditorSessionState::Get().FindTerrainBrush();
		if (!selectedSceneObject && terrainBrush)
		{
			terrainBrush->m_isEditMode = false;
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
				terrainBrush)
			{
				terrainBrush->m_isEditMode = false;
			}

			// ★ range-for가 아니라 인덱스 순회인 이유 (트랙 C · C2)
			//
			// 이 루프 안에서 그리는 드로어가 **같은 오브젝트에 컴포넌트를 붙인다**.
			// 확정된 실사례: ImGuiDrawHelperTerrainComponent가 "Paint Foliage"를 열 때
			// FoliageComponent가 없으면 그 자리에서 owner->AddComponent<FoliageComponent>()를
			// 부른다(ImGuiDrawHelperTerrainComponent.cpp). AddComponent는 m_components에
			// push_back하므로 커패시티를 넘기는 순간 벡터가 재할당되고, range-for가 쥐고
			// 있던 반복자와 component 참조가 그 자리에서 무효해진다 — 드로어가 반환된 뒤
			// 반복자를 증가시키는 것만으로 UB다(이 반복에서는 그 뒤로 component를 더 쓰지
			// 않아 증상이 늦게 나타날 뿐이다).
			//
			// 인덱스는 재할당을 건너도 유효하고, size()를 매 반복 다시 읽으므로 방금 붙은
			// 컴포넌트도 같은 프레임에 자연스럽게 그려진다. 무한 증식은 드로어 쪽 "없을
			// 때만 만든다" 가드가 막는다. 저장소에 이미 있는 관용구다 —
			// Entity::FindComponentSlot이 같은 이유로 인덱스 선형 탐색을 쓴다.
			//
			// 부착을 커맨드 버퍼로 미루는 쪽은 택하지 않았다: 드로어가 반환값을 바로 다음
			// 줄에서 역참조한다(foliage->GetFoliageTypes()). 지연시키면 그 참조가 깨진다.
			for (size_t componentIndex = 0; componentIndex < selectedSceneObject->m_components.size(); ++componentIndex)
			{
				auto& component = selectedSceneObject->m_components[componentIndex];
				if(nullptr == component || component->GetTypeID() == type_guid(RectTransformComponent))
					continue;

				// CT1: 종전 Meta::Find(component->ToString())는 매 프레임 컴포넌트마다
				// 문자열 생성 + 문자열 해시 조회였다 — m_name이 타입명과 일치한다는
				// GENERATED_BODY 관행에 기댄 우회이기도 했다. typeID 조회는 항등이다
				// (Registry가 등록 시 이름 맵·해시 맵에 같은 Type을 넣는다).
				const auto& type = Meta::Find(component->GetTypeID().m_ID_Data);

				std::string componentBaseName = component->ToString();
				if (!type) continue;

				// 체크박스에 m_isEnabled를 직접 물리면 SetEnabled를 건너뛰어
				// OnEnable/OnDisable이 영영 호출되지 않는다. 지역 값으로 받아
				// 전이가 생긴 프레임에만 컴포넌트에 알린다.
				bool isEnabled = component->IsEnabled();
				const bool isHeaderOpen = ImGui::DrawCollapsingHeaderWithButton(componentBaseName.c_str(), ImGuiTreeNodeFlags_DefaultOpen, ICON_FA_BARS, &isOpen, &isEnabled);
				if (isEnabled != component->IsEnabled())
				{
					component->SetEnabled(isEnabled);
				}

				if (isHeaderOpen)
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
					else if (componentTypeID == type_guid(ScriptComponent))
					{
						ScriptComponent* script = dynamic_cast<ScriptComponent*>(component.get());
						if (nullptr != script)
						{
							DrawManagedScripts(script);
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
							//이건 뭔 버그죠?
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
						if (snd) ImGuiDrawHelperSoundComponent(snd);   // 커스텀 인스펙터 호출
					}
					else if (type)
					{
						// K2 스테이지 A: m_components 순회 변수(component)가 이제
						// std::unique_ptr<Component> — dynamic_pointer_cast(shared_ptr
						// 전용) 대신 dynamic_cast로 raw 포인터를 얻는다.
						auto* customInspector = dynamic_cast<ICustomEditor*>(component.get());
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

					// ScriptComponent는 아래 C# Scripts 섹션이 담당한다 —
					// 여기(단일 부착 경로)로 붙이면 두 번째 스크립트부터 기존 것이 반환된다.
					if (type->typeID == type_guid(ScriptComponent))
						continue;

					if (ImGui::MenuItem(type_name.c_str()))
					{
						// K2 스테이지 A: AddComponent가 raw Component*를 돌려준다.
						auto* component = selectedSceneObject->AddComponent(*type);
						if (auto* initializable = dynamic_cast<System::IInitializable*>(component))
						{
							initializable->Initialize();
						}
					}
				}

				// ── C# Scripts ──
				// ClrHost가 스크립트 어셈블리에 등록된 타입 이름을 내준다.
				// 스크립트는 한 오브젝트에 여럿 붙으므로 AddComponentAllowMultiple 경로를 탄다.
				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.f, 1.f), "C# Scripts");

				const auto managedTypeNames = ClrHost::Get().GetBehaviourTypeNames();
				if (managedTypeNames.empty())
				{
					ImGui::TextDisabled(ClrHost::Get().IsReady()
						? "등록된 C# 스크립트가 없습니다"
						: "CLR이 준비되지 않았습니다");
				}
				for (const auto& managedName : managedTypeNames)
				{
					if (!searchFilter.PassFilter(managedName.c_str()))
						continue;

					if (ImGui::MenuItem((managedName + " (C#)").c_str()))
					{
						AttachManagedScript(selectedSceneObject, managedName);
					}
				}

				ImGui::EndPopup();
			}

			// 다음 프레임에서 열기





			if (m_openClipPicker) 
			{
				DrawSoundClipPicker();
			}

			if (isOpen)
			{
				ImGui::OpenPopup("ComponentMenu");
				isOpen = false;
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

						std::ofstream fout(selectedMetaFilePath, std::ios::binary | std::ios::trunc);
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

void InspectorWindow::DrawManagedScripts(ScriptComponent* script)
{
	if (nullptr == script) return;

	auto& clr = ClrHost::Get();

	// 타입 미지정 상태(구 씬에서 온 빈 컴포넌트 등)에서도 여기서 바로 고를 수 있게 한다.
	if (script->m_scriptType.empty())
	{
		ImGui::TextDisabled("스크립트 타입이 지정되지 않았습니다");

		const auto typeNames = clr.GetBehaviourTypeNames();
		if (typeNames.empty())
		{
			ImGui::TextDisabled(clr.IsReady() ? "등록된 C# 스크립트가 없습니다"
											  : "CLR이 준비되지 않았습니다");
			return;
		}

		if (ImGui::BeginCombo("Script Type", "선택..."))
		{
			for (const auto& typeName : typeNames)
			{
				if (ImGui::Selectable(typeName.c_str()))
				{
					script->m_scriptType = typeName;
					script->OnInitialized();
				}
			}
			ImGui::EndCombo();
		}
		return;
	}

	if (!script->HasInstance())
	{
		ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
			"'%s' 인스턴스 없음 (등록되지 않은 타입이거나 CLR 미준비)", script->m_scriptType.c_str());

		// CLR이 뒤늦게 준비됐거나 어셈블리를 다시 읽은 경우를 위한 수동 재시도.
		if (clr.IsReady() && ImGui::Button("인스턴스 다시 만들기"))
		{
			script->OnInitialized();
		}
		return;
	}

	const int instanceId = script->GetInstanceId();
	const int fieldCount = clr.GetFieldCount(instanceId);
	if (0 == fieldCount)
	{
		ImGui::TextDisabled("노출된 필드 없음");
		return;
	}

	// 인스턴스 id로 스코프를 묶어야 같은 이름 필드가 여러 스크립트에 있어도 위젯이 섞이지 않는다.
	ImGui::PushID(instanceId);

	for (int i = 0; i < fieldCount; ++i)
	{
		const std::string name = clr.GetFieldName(instanceId, i);
		ImGui::PushID(i);

		switch (clr.GetFieldType(instanceId, i))
		{
		case ClrHost::ScriptFieldType::Float:
		{
			float value = clr.GetFieldFloat(instanceId, i);
			if (ImGui::DragFloat(name.c_str(), &value, 0.01f))
			{
				clr.SetFieldFloat(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Int32:
		{
			int value = clr.GetFieldInt32(instanceId, i);
			if (ImGui::DragInt(name.c_str(), &value))
			{
				clr.SetFieldInt32(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Bool:
		{
			bool value = clr.GetFieldBool(instanceId, i);
			if (ImGui::Checkbox(name.c_str(), &value))
			{
				clr.SetFieldBool(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Float3:
		{
			ClrHost::ScriptFloat3 value = clr.GetFieldFloat3(instanceId, i);
			if (ImGui::DragFloat3(name.c_str(), &value.x, 0.01f))
			{
				clr.SetFieldFloat3(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::String:
		{
			std::string value = clr.GetFieldString(instanceId, i);

			char buffer[256]{};
			const size_t length = std::min(value.size(), sizeof(buffer) - 1);
			std::memcpy(buffer, value.data(), length);

			if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer)))
			{
				clr.SetFieldString(instanceId, i, buffer);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Object:
		{
			Entity* target = clr.GetFieldObject(instanceId, i);
			const std::string label = (nullptr != target) ? target->m_name.ToString() : std::string("(없음)");

			ImGui::Text("%s", name.c_str());
			ImGui::SameLine();
			ImGui::Button(label.c_str(), ImVec2(-40.f, 0.f));

			// 계층 창에서 끌어다 놓는 것을 받는다. 페이로드 이름은 기존 드래그 소스와 맞춘다.
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Entity"))
				{
					if (payload->Data && payload->DataSize == sizeof(Entity*))
					{
						Entity* dropped = *static_cast<Entity**>(payload->Data);
						clr.SetFieldObject(instanceId, i, dropped);
						script->CaptureFields();
					}
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
			{
				clr.SetFieldObject(instanceId, i, nullptr);
				script->CaptureFields();
			}
			break;
		}
		default:
			ImGui::TextDisabled("%s (지원하지 않는 타입)", name.c_str());
			break;
		}

		ImGui::PopID();
	}

	ImGui::PopID();
}

void InspectorWindow::ImGuiDrawHelperGameObjectBaseInfo(Entity* gameObject)
{
	std::string name = gameObject->m_name.ToString();
	bool isEnabled = gameObject->IsEnabled();
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
		for (int i = 0; i <= layerCount; ++i)
		{
			bool isSelected = false;
			if (i == layerCount) // "Add Layer" 항목
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

void InspectorWindow::ImGuiDrawHelperTransformComponent(Entity* gameObject)
{
	// 현재 트랜스폼 값
	math::vector4 position = gameObject->Transform_().GetPositionValue();
	math::vector4 rotation = gameObject->Transform_().GetRotationValue();
	math::vector4 scale = gameObject->Transform_().GetScaleValue();

	// ===== POSITION =====
	static bool editingPosition = false;
	static math::vector4 prevPosition{};

	const math::vector3 initialEuler = math::to_euler(math::quaternion{
		rotation.x, rotation.y, rotation.z, rotation.w });
	float pyr[3]{ initialEuler.x, initialEuler.y, initialEuler.z }; // pitch yaw roll

	for (float& i : pyr)
	{
		i *= math::rad_to_deg;
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
			gameObject->Transform_().SetPositionValue(
				position, TransformWriteReason::Inspector);
		}
		if (editingPosition && ImGui::IsItemDeactivatedAfterEdit())
		{
			if (prevPosition != position)
			{
				Meta::MakeCustomChangeCommand([=]
				{
					gameObject->Transform_().SetPositionValue(
						prevPosition, TransformWriteReason::Inspector);
				},
				[=]
				{
					gameObject->Transform_().SetPositionValue(
						position, TransformWriteReason::Inspector);
				});
			}
			editingPosition = false;
		}

		static bool editingRotation = false;
		static math::vector4 prevRotation{};
		static float prevEuler[3] = {};

		const math::vector3 currentEuler = math::to_euler(math::quaternion{
			rotation.x, rotation.y, rotation.z, rotation.w });
		float pyr[3]{ currentEuler.x, currentEuler.y, currentEuler.z };
		float deltaEuler[3] = { 0, 0, 0 };

		float prevPYR[3];
		prevRotation = rotation;

		for (float& i : pyr) i *= math::rad_to_deg;
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
			const math::vector3 radianEuler{
				math::radians(pyr[0] - prevPYR[0]),
				math::radians(pyr[1] - prevPYR[1]),
				math::radians(pyr[2] - prevPYR[2]) };
			const math::quaternion delta = math::quaternion_from_pitch_yaw_roll(
				radianEuler.x, radianEuler.y, radianEuler.z);
			const math::quaternion current{
				rotation.x, rotation.y, rotation.z, rotation.w };
			const math::quaternion combined = delta * current;
			rotation = math::vector4{
				combined.x, combined.y, combined.z, combined.w };
			gameObject->Transform_().SetRotationValue(
				rotation, TransformWriteReason::Inspector);
		}
		if (editingRotation && ImGui::IsItemDeactivatedAfterEdit())
		{
			const math::quaternion compareQuaternion =
				math::quaternion_from_pitch_yaw_roll(
					math::radians(prevEuler[0]), math::radians(prevEuler[1]),
					math::radians(prevEuler[2]));
			const math::vector4 compare{
				compareQuaternion.x, compareQuaternion.y,
				compareQuaternion.z, compareQuaternion.w };
			if (compare != rotation)
			{
				Meta::MakeCustomChangeCommand([=]
				{
					gameObject->Transform_().SetRotationValue(
						prevRotation, TransformWriteReason::Inspector);
				},
				[=]
				{
					gameObject->Transform_().SetRotationValue(
						rotation, TransformWriteReason::Inspector);
				});
			}
			editingRotation = false;
		}

		static bool editingScale = false;
		static math::vector4 prevScale{};

		ImGui::Text("Scale     ");
		ImGui::SameLine();
		if (ImGui::DragFloat3("##Scale", &scale.x, 0.1f, 0.001f, 1000.f))
		{
			if (!editingScale)
			{
				prevScale = scale;
				editingScale = true;
			}
			gameObject->Transform_().SetScaleValue(
				scale, TransformWriteReason::Inspector);
		}
		if (editingScale && ImGui::IsItemDeactivatedAfterEdit())
		{
			if (prevScale != scale)
			{
				Meta::MakeCustomChangeCommand([=]
				{
					gameObject->Transform_().SetScaleValue(
						prevScale, TransformWriteReason::Inspector);
				},
				[=]
				{
					gameObject->Transform_().SetScaleValue(
						scale, TransformWriteReason::Inspector);
				});
			}
			editingScale = false;
		}

		{
			gameObject->Transform_().UpdateLocalMatrix();
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
			gameObject->Transform_().SetPositionValue(
				{ 0.f, 0.f, 0.f, 1.f }, TransformWriteReason::Inspector);
			gameObject->Transform_().SetRotationValue(
				{ 0.f, 0.f, 0.f, 1.f }, TransformWriteReason::Inspector);
			gameObject->Transform_().SetScaleValue(
				{ 1.f, 1.f, 1.f, 1.f }, TransformWriteReason::Inspector);
			gameObject->Transform_().UpdateLocalMatrix();
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
			auto type = Meta::Find(type_guid(ShadowMapPassSetting)) /* CT1: typeID */;
			Meta::TypedDraw::DrawOwnMembers(profile.settings.shadow);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("SSAOPass"))
		{
			ImGui::PushID("SSAOPass");
			auto type = Meta::Find(type_guid(SSAOPassSetting));
			Meta::TypedDraw::DrawOwnMembers(profile.settings.ssao);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("DeferredPass"))
		{
			ImGui::PushID("DeferredPass");
			auto type = Meta::Find(type_guid(DeferredPassSetting));
			Meta::TypedDraw::DrawOwnMembers(profile.settings.deferred);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("SSGIPass"))
		{
			ImGui::PushID("SSGIPass");
			auto type = Meta::Find(type_guid(SSGIPassSetting));
			Meta::TypedDraw::DrawOwnMembers(profile.settings.ssgi);
			ImGui::PopID();
		}

		if (ImGui::CollapsingHeader("SkyBoxPass"))
		{
			ImGui::Checkbox("Use SkyBox", &profile.settings.m_isSkyboxEnabled);

			file::path HDRPath = PathFinder::Relative("HDR\\");
			std::string_view profileTextureName = profile.settings.skyboxTextureName;
			const RenderPassSettings runtimeRenderSettings =
				RuntimeSettings::Get().GetRenderPassSettings();
			std::string_view settingsTextureName = runtimeRenderSettings.skyboxTextureName;
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
				auto type = Meta::Find(type_guid(AAPassSetting));
				Meta::TypedDraw::DrawOwnMembers(profile.settings.aa);
				ImGui::PopID();
			}

			if (ImGui::CollapsingHeader("BloomPass"))
			{
				ImGui::PushID("BloomPass");
				auto type = Meta::Find(type_guid(BloomPassSetting));
				Meta::TypedDraw::DrawOwnMembers(profile.settings.bloom);
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
				auto type = Meta::Find(type_guid(VignettePassSetting));
				Meta::TypedDraw::DrawOwnMembers(profile.settings.vignette);
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
				auto type = Meta::Find(type_guid(ColorGradingPassSetting));
				Meta::TypedDraw::DrawOwnMembers(profile.settings.colorGrading);
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
			EditorAssetDatabase::Get().SaveExistingVolumeProfile(
				volumeComponent->m_volumeProfileGuid, &profile);
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
		ImGui::Image((ImTextureID)EditorImGuiTexture::From(decalComponent->GetDecalTexture()), ImVec2(30, 30));
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
		ImGui::Image((ImTextureID)EditorImGuiTexture::From(decalComponent->GetNormalTexture()), ImVec2(30, 30));
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
		ImGui::Image((ImTextureID)EditorImGuiTexture::From(decalComponent->GetORMTexture()), ImVec2(30, 30));
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
	ImGui::ColorEdit4("color tint", &imageComponent->color.r);
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

	ImGui::Text("Navigation");
	auto drawNavigationTarget = [imageComponent](Direction direction)
	{
		if (Entity* target = imageComponent->GetNextNavi(direction))
			ImGui::Text(("-> " + target->m_name.ToString()).c_str());
		else
			ImGui::Text("-> None");
	};

	ImGui::Button(ICON_FA_ARROW_LEFT "##Left", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				Entity* draggedObject = Entity::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Left, draggedObject);
			}
		}
	}
	ImGui::SameLine();
	drawNavigationTarget(Direction::Left);

	ImGui::Button(ICON_FA_ARROW_RIGHT "##Right", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				Entity* draggedObject = Entity::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Right, draggedObject);
			}
		}
	}
	ImGui::SameLine();
	drawNavigationTarget(Direction::Right);

	ImGui::Button(ICON_FA_ARROW_UP "##Up", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				Entity* draggedObject = Entity::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Up, draggedObject);
			}
		}
	}
	ImGui::SameLine();
	drawNavigationTarget(Direction::Up);
	ImGui::Button(ICON_FA_ARROW_DOWN "##Down", ImVec2(30, 20));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
		{
			Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
			if (draggedIndex != imageComponent->GetOwner()->m_index)
			{
				Entity* draggedObject = Entity::FindIndex(draggedIndex);
				imageComponent->SetNavi(Direction::Down, draggedObject);
			}
		}
	}
	ImGui::SameLine();
	drawNavigationTarget(Direction::Down);
}

void InspectorWindow::ImGuiDrawHelperSpriteRenderer(SpriteRenderer* spriteRenderer)
{
	if (spriteRenderer->GetSprite() == nullptr)
		ImGui::Button("None Sprite", ImVec2(150, 20));
	else
		ImGui::Image((ImTextureID)EditorImGuiTexture::From(spriteRenderer->GetSprite()), ImVec2(30, 30));
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

	if (const auto* type = Meta::Find(type_guid(SpriteRenderer)))
	{
		Meta::TypedDraw::DrawOwnMembers(*spriteRenderer);
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

	// ─────────────────────────────────────────────────────────────
	//  Clip / Picker
	// ─────────────────────────────────────────────────────────────
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

	// ─────────────────────────────────────────────────────────────
	//  Bus / Basic Params
	// ─────────────────────────────────────────────────────────────
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

	// 루프 상태 변경 즉시 채널에 반영
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

	// ─────────────────────────────────────────────────────────────
	//  Spatial
	// ─────────────────────────────────────────────────────────────
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

		// 그래프: spatial이면 항상 표시
		ImGui::SeparatorText("Distance Rolloff Curve");

		const bool isCustom = (sc->rolloff == Rolloff::Custom);

		// Linear /Inverse 선택 시: 자동 곡선으로 동기화(읽기전용)
		if (!isCustom) {
			if (rollChanged || minBefore != sc->minDistance || maxBefore != sc->maxDistance || sc->localRolloffCurve.size() < 2) {
				if (sc->rolloff == Rolloff::Linear)  BuildLinearCurve(sc->localRolloffCurve, sc->minDistance, sc->maxDistance);
				if (sc->rolloff == Rolloff::Inverse) BuildInverseCurve(sc->localRolloffCurve, sc->minDistance, sc->maxDistance);
			}
			DrawRolloffCurveEditor(sc->localRolloffCurve, std::max(0.1f, sc->maxDistance), ImVec2(0, 200), nullptr, /*readOnly=*/true);
			ImGui::TextDisabled("Rolloff is %s - curve preview (read-only).", sc->rolloff == Rolloff::Linear ? "Linear" : "Inverse");
		}
		else {
			// Custom: 에디트 가능
			if (sc->localRolloffCurve.size() < 2) {
				sc->localRolloffCurve = { {0.f,1.f}, { std::max(0.1f, sc->maxDistance), 0.f } };
			}
			// maxDistance 변경 시 마지막 점 X를 범위 내로 보정(편집 내용은 유지)
			sc->localRolloffCurve.back().distance = std::clamp(sc->localRolloffCurve.back().distance, 0.1f, std::max(0.1f, sc->maxDistance));

			if (ImGui::SmallButton("Reset to Default")) {
				sc->localRolloffCurve = { {0.f,1.f}, { std::max(0.1f, sc->maxDistance), 0.f } };
			}
			DrawRolloffCurveEditor(sc->localRolloffCurve, std::max(0.1f, sc->maxDistance), ImVec2(0, 200), nullptr, /*readOnly=*/false);
			ImGui::TextDisabled("Custom mode - drag points, double-click to add, right-click/Delete to remove.");
		}

		// 실시간 3D 채널 반영(위치/거리/롤오프 모드 등)
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

	// ─────────────────────────────────────────────────────────────
	//  Reverb Send
	// ─────────────────────────────────────────────────────────────
	SeparatorText("Reverb Send");
	bool useRevBefore = sc->useReverbSend;
	Checkbox("Enable Reverb Send", &sc->useReverbSend);

	// dB 슬라이더(-80~+10), 내부는 선형(0~1)로 변환해서 FMOD에 적용
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
	// 값이 바뀌면 항상 적용
	if (IsItemEdited() || IsItemDeactivatedAfterEdit()) {
		applyReverb(sc->Get2DChannel());
		applyReverb(sc->Get3DChannel());
	}

	// ─────────────────────────────────────────────────────────────
	//  Preview Controls
	// ─────────────────────────────────────────────────────────────
	Separator();
	if (Button("Play")) { sc->Play(); } ImGui::SameLine();
	if (Button("Stop")) { sc->Stop(); } ImGui::SameLine();
	if (Button("OneShot")) { sc->PlayOneShot(); }

	// 볼륨/피치/프라이어리티 변경 실시간 반영(채널 살아있을 때)
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
		// readOnly 모드: 상호작용 없음, 안내만
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

	// 독립 윈도우(모달 느낌)
	SetNextWindowSize(ImVec2(520, 480), ImGuiCond_Appearing);
	if (Begin("Select Audio Clip", &m_openClipPicker,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
	{
		// 상단: 검색/리프레시
		if (InputTextWithHint("##search", "Search clip key...", &m_clipSearch)) {
			// 입력 시 즉시 필터 반영
		}
		SameLine();
		if (Button("Refresh")) {
			m_clipKeyCache = Sound->getAllClipKeys();
		}
		Separator();

		// 필터링
		auto toLower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
		std::string q = toLower(m_clipSearch);

		// 리스트 영역
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

			// 우측 프리뷰 버튼
			if (IsItemHovered() && IsMouseDoubleClicked(0)) 
			{
				// 더블클릭 = 선택 확정
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
					// 미리듣기: 현재 타겟 버스/볼륨/피치 사용
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

		// 하단 버튼
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



