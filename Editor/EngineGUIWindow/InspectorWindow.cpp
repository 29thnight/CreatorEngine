#include "../EngineEntry/EditorProjectOperations.h"
#include "EditorTheme.h"
#include "InspectorWindow.h"
#include "InspectorControl.h"
#include "InspectorLayoutFixture.h"
#include <atomic>
#include <mutex>
#include "EditorAssetDragPayload.h"

#include "EditorInspectorPanel.h"
#include "InspectorIconList.h"
#include "EditorWindowNames.h"
#include "Windows/EditorStandardWindows.h"
#include "EditorMenuDraw.h"
#include "EditorObjectOperations.h"
#include "EditorScriptAuthoring.h"
#include "EditorPlatform.h"
#include "EditorAssetPresentation.h"
#include "GameObjectCommand.h"
#include "Animator.h"
#include "PhysicsBodyComponent.h"
#include "AuthoringNodeEquality.h"
#include "AuthoringParsedDocument.h"
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
#include "EditorSectionHeader.h"
#include "EditorAxisField3.h"
#include "Terrain.h"
#include "FileDialog.h"
#include "TagManager.h"
#include "PlayerInput.h"
#include "InputActionManager.h"
#include "SoundSystem.h"
#include <mathematics/scalar.hpp>
//----------------------------
#include "ExternUI.h"
#include "StateMachineComponent.h"
#include "BehaviorTreeComponent.h"
#include "FoliageComponent.h"
#include "SceneRenderProfileComponent.h"
#include "RectTransformComponent.h"
#include "DecalComponent.h"
#include "SpriteRenderer.h"
#include "SoundComponent.h"
#include "AudioListenerComponent.h"
//----------------------------

#include "EditorIcons.h"
#include "NodeEditor.h"
#include <algorithm>
#include "imgui_stdlib.h"

namespace ed = ax::NodeEditor;

ed::EditorContext* m_fsmEditorContext{nullptr};
bool s_CreatingLink = false;
ed::PinId s_LinkStartPin = 0;
ed::LinkId s_EditLinkId = 0;
bool s_RenameNodePopup{false};

ed::EditorContext* s_BTEditorContext{nullptr};

// CT6-c: typed Draw 등록 — 전 타입의 위젯 트리 인스턴스화를 이 TU 한 곳에
// 가둔다. 목록은 등록 정본(RegisterReflectManual.h)의 X-매크로를 공유한다.
static void RegisterAllTypedDraws()
{
#define REFLECT_DRAW_ONE(T) Meta::TypedDraw::RegisterDraw<T>();
    REFLECT_TYPE_LIST(REFLECT_DRAW_ONE)
#undef REFLECT_DRAW_ONE
}

namespace
{
Authoring::WriteDocument physics_shape_document(std::span<const PhysicsShapeDefinition> shapes)
{
    Authoring::WriteDocument document;
    document.Root().SetSequence();
    for (auto shape : shapes)
    {
        auto item = Meta::SerializeDocument(&shape);
        document.Root().Append().Assign(item.Root().Read());
    }
    return document;
}

void draw_physics_shapes(PhysicsBodyComponent& body)
{
    if (!Meta::Find(type_guid(PhysicsShapeDefinition)))
    {
        ImGui::TextDisabled("Physics shape schema unavailable");
        return;
    }
    struct Draft
    {
        std::vector<PhysicsShapeDefinition> shapes;
        Authoring::WriteDocument before;
        std::string failure;
    };
    static EntityHandle owner;
    static std::unordered_map<std::uint64_t, Draft> drafts;
    const auto target = body.GetOwner()->GetScene()->HandleOf(body.GetOwner()->m_index);
    if (target != owner)
    {
        drafts.clear();
        owner = target;
    }
    const auto id = body.GetInstanceID();
    const auto reload = [&]() {
        Draft value;
        value.shapes.assign(body.Shapes().begin(), body.Shapes().end());
        value.before = physics_shape_document(body.Shapes());
        drafts.insert_or_assign(id, std::move(value));
    };
    if (!drafts.contains(id))
        reload();
    auto& draft = drafts.at(id);
    auto current = physics_shape_document(body.Shapes());
    auto pending = physics_shape_document(draft.shapes);
    if (!Authoring::NodesEqual(current.Root().Read(), draft.before.Root().Read()) &&
        Authoring::NodesEqual(pending.Root().Read(), draft.before.Root().Read()))
        reload();
    ImGui::SeparatorText("Collision Shapes");
    ImGui::BeginDisabled(SceneManagers->IsGameStart() || EditorObjectOperations::IsEditLocked(body.GetOwner(), true));
    std::size_t remove = draft.shapes.size();
    for (std::size_t index = 0; index < draft.shapes.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::TreeNode("Shape", "Shape %u", draft.shapes[index].shapeId))
        {
            auto& shape = draft.shapes[index];

            constexpr const char* kinds[]{"Box", "Sphere", "Capsule", "Convex", "Triangle Mesh", "Heightfield"};
            int kind = std::to_underlying(shape.kind);

            if (ImGui::Combo("Shape Type", &kind, kinds, 6))
                shape.kind = static_cast<PhysicsShapeKind>(kind);

            const auto project = SceneManagers->ProjectLayers();
            const auto settings = project ? project->Snapshot() : nullptr;
            const auto* layer = settings ? settings->catalog.Find(ce::layers::layer_id{shape.layerOverride}) : nullptr;
            const char* preview = shape.layerOverride == 0 ? "Inherit Entity Layer"
                                  : layer                  ? layer->name.c_str()
                                                           : "Missing Layer";
            if (ImGui::BeginCombo("Layer", preview))
            {
                if (ImGui::Selectable("Inherit Entity Layer", shape.layerOverride == 0))
                    shape.layerOverride = 0;
                if (settings)
                    for (const auto& item : settings->catalog.definitions)
                        if (item && !item->retired &&
                            ImGui::Selectable(item->name.c_str(), shape.layerOverride == item->id.value))
                            shape.layerOverride = item->id.value;
                ImGui::EndCombo();
            }
            Meta::TypedDraw::DrawOwnMembers(shape);
            if (ImGui::Button("Remove Shape"))
                remove = index;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove < draft.shapes.size())
        draft.shapes.erase(draft.shapes.begin() + remove);
    if (ImGui::Button("Add Shape") && draft.shapes.size() < 65535)
    {
        std::uint32_t next = 0;
        for (const auto& shape : draft.shapes)
            next = (std::max)(next, shape.shapeId);
        if (next != UINT32_MAX)
        {
            PhysicsShapeDefinition shape;
            shape.shapeId = next + 1;
            draft.shapes.push_back(shape);
        }
        else
            draft.failure = "Shape ID limit reached";
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Shapes"))
    {
        auto document = physics_shape_document(draft.shapes);
        const auto result = EditorObjectOperations::PhysicsShapes(target, "#" + std::to_string(id),
                                                                  document.Root().Read().Dump(), &draft.before);
        if (result.IsSuccess())
            reload();
        else
            draft.failure = result.message;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Shapes"))
        reload();
    ImGui::EndDisabled();
    if (!draft.failure.empty())
        ImGui::TextWrapped("%s", draft.failure.c_str());
    ImGui::TextDisabled("Apply saves the complete shape list as one Undo entry.");
}

void draw_character_movement(CharacterMovementComponent& character, editor::widgets::property_layout_state& layoutState)
{
    const auto target = character.GetOwner()->GetScene()->HandleOf(character.GetOwner()->m_index);
    auto document = Meta::SerializeDocument(&character);
    const auto root = document.Root().Read();
    const auto fieldText = [](Authoring::ReadNode node) {
        std::string error;
        auto parsed = Authoring::ParsedDocument{};
        if (node.IsScalar() && node.Scalar().starts_with("{"))
        {
            parsed = Authoring::ParsedDocument::ParseText(node.AsString(), error);
            if (parsed) node = parsed.Root();
        }
        if (node.IsMap())
            return std::string(node["x"].AsString()) + ", " + std::string(node["y"].AsString()) + ", " +
                   std::string(node["z"].AsString());
        return std::string(node.AsString());
    };
    static EntityHandle previousOwner;
    static std::unordered_map<std::string, std::string> drafts;
    static std::string failure;
    if (previousOwner != target)
    {
        drafts.clear();
        failure.clear();
        previousOwner = target;
    }

    ImGui::TextWrapped("World +Y capsule. Positive uniform scale required.");
    ImGui::BeginDisabled(SceneManagers->IsGameStart() ||
                         EditorObjectOperations::IsEditLocked(character.GetOwner(), true));
    const std::pair<const char*, const char*> fields[]{
        {"m_radius", "Radius (m)"}, {"m_cylinderHeight", "Cylinder height (m)"},
        {"m_contactOffset", "Contact offset (m)"}, {"m_stepOffset", "Step offset (m)"},
        {"m_slopeLimitCosine", "Slope limit cosine (0 disables)"}, {"m_gravity", "Gravity (m/s squared)"},
        {"m_minimumDistance", "Minimum move (m)"}, {"m_acceleration", "Acceleration (m/s squared)"},
        {"m_brakingDecay", "Braking decay (1/s)"}, {"m_jumpSpeed", "Jump speed (m/s)"},
        {"m_maxFallSpeed", "Maximum fall speed (m/s)"}, {"m_initialVelocity", "Initial velocity (x,y,z m/s)"}};
    const editor::widgets::property_sheet sheet(layoutState,
        {"Radius (m)", "Cylinder height (m)", "Contact offset (m)", "Step offset (m)",
         "Slope limit cosine (0 disables)", "Gravity (m/s squared)", "Minimum move (m)",
         "Acceleration (m/s squared)", "Braking decay (1/s)", "Jump speed (m/s)",
         "Maximum fall speed (m/s)", "Initial velocity (x,y,z m/s)"});

    for (const auto& [field, label] : fields)
    {
        const auto key = std::to_string(character.GetInstanceID()) + field;
        auto [entry, inserted] = drafts.try_emplace(key, fieldText(root[field]));
        ImGui::PushID(field);
        ImGui::SetNextItemWidth(sheet.line(label));
        ImGui::InputText("##Value", &entry->second);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const auto result = EditorObjectOperations::Property(target,
                "#" + std::to_string(character.GetInstanceID()), field, entry->second);
            failure = result.IsSuccess() ? std::string{} : result.message;
            auto current = Meta::SerializeDocument(&character);
            entry->second = fieldText(current.Root().Read()[field]);
        }
        if (!ImGui::IsItemActive())
            entry->second = fieldText(root[field]);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (!failure.empty())
        ImGui::TextWrapped("%s", failure.c_str());
    ImGui::TextWrapped("Commit on focus loss. Each field uses validated Undo/Redo.");
}

struct ComponentMenuVisual
{
    const Texture* image{};
    const char* fallback{};
};

    ComponentMenuVisual component_category_visual(std::string_view category)
	{
		auto& images = EditorAssetPresentation::Get();
		using FileType = EditorAssetPresentation::FileType;
		if (category == "Rendering") return {images.GetFileIcon(FileType::Model), EditorIcon::Model};
		if (category == "Physics") return {images.GetEntityIcon("trigger"), EditorIcon::Layers};
		if (category == "Animation") return {images.GetEntityIcon("player"), EditorIcon::AvatarMask};
		if (category == "Audio") return {images.GetEntityIcon("audio"), EditorIcon::Audio};
		if (category == "AI") return {images.GetEntityIcon("game-manager"), EditorIcon::Hierarchy};
		if (category == "Input") return {images.GetProjectIcon(), EditorIcon::Game};
		if (category == "UI") return {images.GetFileIcon(FileType::Texture), EditorIcon::Texture};
		if (category == "Scripts") return {images.GetEntityIcon("script"), EditorIcon::Script};
		return {images.GetEntityIcon("prefab"), EditorIcon::GameObject};
	}

	ComponentMenuVisual component_entry_visual(const editor::components::Entry& entry)
	{
		auto& images = EditorAssetPresentation::Get();
		using FileType = EditorAssetPresentation::FileType;
		if (entry.managed) return component_category_visual("Scripts");
		if (entry.type == "CameraComponent") return {images.GetEntityIcon("camera"), EditorIcon::Camera};
		if (entry.type == "LightComponent") return {images.GetEntityIcon("light"), EditorIcon::Lit};
		if (entry.type == "SpriteRenderer" || entry.type == "SpriteSheetComponent" || entry.type == "ImageComponent")
			return {images.GetFileIcon(FileType::Texture), EditorIcon::Texture};
		if (entry.type == "TerrainComponent" || entry.type == "FoliageComponent")
			return {images.GetFileIcon(FileType::TerrainTexture), EditorIcon::Terrain};
		if (entry.type == "DecalComponent") return {images.GetFileIcon(FileType::MaterialTexture), EditorIcon::Material};
		if (entry.type == "SceneRenderProfileComponent") return {images.GetFileIcon(FileType::SceneRenderProfile), EditorIcon::RenderProfile};
		if (entry.type == "TextComponent") return {images.GetFileIcon(FileType::Font), EditorIcon::Font};
		return component_category_visual(entry.category);
	}

	void draw_component_menu_row(const ComponentMenuVisual& visual, const char* label, bool category)
	{
		const auto min = ImGui::GetItemRectMin();
		const auto max = ImGui::GetItemRectMax();
		const float size = ImGui::GetFontSize();
		const float gap = editor::ThemePixels(7.f);
		const float y = min.y + (max.y - min.y - size) * .5f;
		auto* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(min, max, true);
		const auto image = visual.image ? EditorImGuiTexture::From(visual.image) : 0;
		if (image) draw->AddImage(image, {min.x, y}, {min.x + size, y + size}, {0,0}, {1,1}, ImGui::GetColorU32(ImVec4(1,1,1,1)));
		else draw->AddText({min.x,y}, ImGui::GetColorU32(ImGuiCol_Text), visual.fallback);
		const float endX = category ? max.x - size - gap : max.x;
		draw->PushClipRect({min.x + size + gap, min.y}, {endX, max.y}, true);
		draw->AddText({min.x + size + gap,y}, ImGui::GetColorU32(ImGuiCol_Text), label);
		draw->PopClipRect();
		if (category) draw->AddText({max.x-size,y}, ImGui::GetColorU32(ImGuiCol_TextDisabled), EditorIcon::Collapse);
		draw->PopClipRect();
	}

	// 창 상태의 유일한 자리(PHASE 21 W3). 팝업 깃발 넷·피커 대상·검색어·캐시 여덟뿐이다.
	InspectorWindow& inspector_state()
	{
		static InspectorWindow value;
		return value;
	}

	// ── W2-I 창구(InspectorControl.h) ───────────────────────────────────────
	std::atomic<float> g_inspectorWidth{ 0.f };
	std::atomic<bool> g_inspectorExpandAll{ false };
	std::atomic<bool> g_inspectorFixture{ false };
	std::mutex g_inspectorMailboxMutex;
	editor::windows::inspector_snapshot g_inspectorSnapshot{};
	// 그리는 스레드만 만진다. 프레임 끝에 사본으로 게시한다.
	std::vector<editor::windows::inspector_body> g_inspectorBodies;
	std::string g_inspectorEntity;

	// 본문 하나가 차지한 가로 범위를 잰다.
	//
	// `CursorMaxPos` 는 창이 그린 항목 오른쪽 끝의 누적 최대값이다. 본문에 들어갈 때
	// 지금 커서로 내려 두고 나올 때 읽은 뒤 원래 값과 합친다 — 앞 본문의 끝이 이
	// 본문의 끝으로 읽히지 않게 하되, 창의 내용 크기 계산은 그대로 둔다.
	// 고정 폭 입력칸이 좁은 폭에서 작업 영역 밖으로 나가면 여기서 넘침이 된다.
	class inspector_body_probe
	{
	public:
		inspector_body_probe()
			: m_window(ImGui::GetCurrentWindow())
			, m_savedMax(m_window->DC.CursorMaxPos)
			, m_start(m_window->DC.CursorPos)
			, m_lines(editor::widgets::property_line_count())
			, m_tally(editor::widgets::take_property_field_tally())
		{
			m_window->DC.CursorMaxPos = m_start;
		}

		void finish(std::string type, std::uint32_t instance, bool open, bool enabledToggle = false)
		{
			const ImVec2 extent = m_window->DC.CursorMaxPos;
			m_window->DC.CursorMaxPos = ImMax(m_savedMax, extent);
			editor::windows::inspector_body body;
			body.type = std::move(type);
			body.instance = instance;
			body.open = open;
			body.enabledToggle = enabledToggle;
			body.propertyLines = editor::widgets::property_line_count() - m_lines;
			// 이 본문 몫만 떠내고, 바깥 본문이 이어 세도록 둘을 되돌린다.
			const auto mine = editor::widgets::take_property_field_tally();
			body.fields = mine.fields;
			body.minLineValue = mine.min_line_value;
			body.minFieldWidth = mine.min_field_width;
			body.narrowestField = mine.min_field_label;
			body.axisFields = mine.axis_fields;
			body.minAxisWidth = mine.min_axis_width;
			body.narrowestAxis = mine.min_axis_label;
			body.firstFieldX = mine.first_field_x;
			body.firstFieldY = mine.first_field_y;
			body.firstFieldW = mine.first_field_w;
			body.firstFieldH = mine.first_field_h;
			body.fieldDigest = mine.digest;
			editor::widgets::merge_property_field_tally(m_tally);
			editor::widgets::merge_property_field_tally(mine);
			body.minX = m_start.x;
			body.maxX = extent.x;
			body.height = m_window->DC.CursorPos.y - m_start.y;
			body.overflow = ImMax(0.f, extent.x - m_window->WorkRect.Max.x);
			g_inspectorBodies.push_back(std::move(body));
		}

	private:
		ImGuiWindow* m_window;
		ImVec2 m_savedMax;
		ImVec2 m_start;
		std::uint64_t m_lines;
		editor::widgets::property_field_tally m_tally;
	};
    } // namespace

void editor::windows::set_inspector_width(float logicalWidth) noexcept
{
	g_inspectorWidth.store(logicalWidth > 0.f ? logicalWidth : 0.f, std::memory_order_relaxed);
}

void editor::windows::set_inspector_expand_all(bool expand) noexcept
{
	g_inspectorExpandAll.store(expand, std::memory_order_relaxed);
}

bool editor::windows::inspector_expand_all() noexcept
{
	return g_inspectorExpandAll.load(std::memory_order_relaxed);
}

void editor::windows::set_inspector_fixture(bool enabled) noexcept
{
	g_inspectorFixture.store(enabled, std::memory_order_relaxed);
}

editor::windows::inspector_snapshot editor::windows::read_inspector()
{
	std::lock_guard lock(g_inspectorMailboxMutex);
	return g_inspectorSnapshot;
}

void editor::windows::draw_inspector()
{
	g_inspectorBodies.clear();
	g_inspectorEntity.clear();

	// 요청한 논리 폭의 영역 안에서 그린다. 세로 막대를 늘 세워 두어 내용 길이에 따라
	// 작업 폭이 흔들리지 않게 하고, 막대 폭만큼 더 잡아 작업 영역이 요청 폭이 되게 한다.
	const float requested = g_inspectorWidth.load(std::memory_order_relaxed);
	const bool probed = requested > 0.f;
	bool visible = true;
	if (probed)
	{
		// ★ 이 자식 창은 **탐색 범위를 가른다**(W2-I5 에서 실측). 폭을 정한 채로는
		//   Tab 이 본문 안으로 들어가지 못한다 — 넣은 키 10 회에 `navId` 가 한 번도
		//   움직이지 않았다. `ImGuiChildFlags_NavFlattened` 는 스크롤하는 자식에는
		//   쓸 수 없어(ImGui 의 제약) 여기서는 길이 아니다.
		//   그래서 폭을 정한 채의 편집 자극은 키가 아니라 포인터로 한다
		//   (`editor.nav pointer`·`press` — 값 칸의 사각형을 장부가 낸다).
		//   키보드 탐색 계약 자체는 폭 창구 없이 도는 `verify-editor-keyboard-nav`
		//   가 지킨다.
		visible = ImGui::BeginChild("##InspectorWidthProbe",
			ImVec2(editor::ThemePixels(requested) + ImGui::GetStyle().ScrollbarSize, 0.f),
			ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	}
	if (visible)
	{
		inspector_state().Draw();
	}
	const ImGuiWindow* const window = ImGui::GetCurrentWindow();
	const float contentWidth = window->WorkRect.GetWidth();
	const float contentMaxX = window->WorkRect.Max.x;
	// 잘린 뒤 남는 폭. 자식 창이 부모 도크보다 넓으면 그 차이는 화면에 없다.
	const float visibleWidth = ImMax(0.f,
		ImMin(window->ClipRect.Max.x, contentMaxX) - window->WorkRect.Min.x);
	if (probed)
	{
		ImGui::EndChild();
	}

	std::lock_guard lock(g_inspectorMailboxMutex);
	++g_inspectorSnapshot.frames;
	g_inspectorSnapshot.entity = g_inspectorEntity;
	g_inspectorSnapshot.uiScale = editor::ThemePixels(1.f);
	g_inspectorSnapshot.requestedWidth = requested;
	g_inspectorSnapshot.expandAll = editor::windows::inspector_expand_all();
	g_inspectorSnapshot.fixture = g_inspectorFixture.load(std::memory_order_relaxed);
	g_inspectorSnapshot.contentWidth = contentWidth;
	g_inspectorSnapshot.contentMaxX = contentMaxX;
	g_inspectorSnapshot.visibleWidth = visibleWidth;
	g_inspectorSnapshot.valueMin = editor::widgets::property_value_min_width();
	g_inspectorSnapshot.axisValueMin = editor::widgets::property_axis_value_min_width();
	g_inspectorSnapshot.activeId = static_cast<std::uint32_t>(ImGui::GetActiveID());
	g_inspectorSnapshot.bodies = g_inspectorBodies;
}

// typed Draw 등록은 창의 일이 아니라 부팅의 일이다(PHASE 21 W3).
//
// 옛 자리는 이 창의 생성자였고 `EditorMain` 이 부팅에서 창을 만들었기에
// 우연히 이르게 돌았다. 소유자가 사라지면 상태는 **처음 그릴 때** 서므로,
// 그대로 두면 인스펙터를 열기 전에는 표가 비어 있다. 그 표를 읽는 것은
// 인스펙터만이 아니다 — 애니메이터 창과 메시 렌더러 헬퍼가 같은
// `Meta::TypedDraw` 를 읽는다. 그래서 부팅 자리로 올린다.
void editor::windows::register_inspector_typed_draws()
{
	RegisterAllTypedDraws();

	// 아이콘 표도 같은 자리에서 채운다. 그리는 쪽은 런타임 타입 ID 만 들고
	// 있으므로 표가 미리 서 있어야 한다 — 첫 프레임에 채우면 그 프레임의
	// 컴포넌트 머리줄이 전부 아이콘 없이 그려진다.
	editor::inspector::register_inspector_icons();
}

void InspectorWindow::DrawAddComponent(Entity* entity)
{
    using namespace editor::components;
    const auto target = entity->GetScene()->HandleOf(entity->m_index);
    const float available = ImMax(ImGui::GetContentRegionAvail().x, 1.f);
    const float naturalWidth = ImGui::CalcTextSize("Add Component").x + ImGui::GetStyle().FramePadding.x * 2.f;
    const char* label = available >= naturalWidth ? "Add Component###AddComponentButton" : "Add\nComponent###AddComponentButton";
    const float width = ImMin(available, ImMax(editor::ThemePixels(160.f), naturalWidth));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) * .5f);
    if (ImGui::Button(label, {width, 0}))
    {
        m_addComponentTarget = target;
        m_componentSearch.clear();
        m_componentCategory.clear();
        m_componentError.clear();
        m_componentSelection = 0;
        m_newScriptPage = false;
        m_componentCatalog.clear();
        for (const auto& [name, type] : ComponentFactorys->m_componentTypes)
            if (type && !name.empty() && Meta::TypeIDOf(*type) != type_guid(ScriptComponent))
                m_componentCatalog.push_back(Native(name));
        for (auto& name : ClrHost::Get().GetComponentTypeNames()) m_componentCatalog.push_back(Script(name));
        Sort(m_componentCatalog);
        ImGui::OpenPopup("AddComponent");
    }

    const auto status = EditorScriptAuthoring::GetStatus();
    if (!status.message.empty())
    {
        ImGui::TextWrapped("%s", status.message.c_str());
        if (status.busy)
        {
            if (ImGui::SmallButton("Cancel compilation")) EditorScriptAuthoring::Cancel();
        }
        else if (!status.succeeded && !status.source.empty())
        {
            if (ImGui::SmallButton("Retry compilation")) EditorScriptAuthoring::Retry();
        }
        if (!status.source.empty())
        {
            if (ImGui::SmallButton("Open script")) EditorPlatform::Get().OpenFile(file::u8path(status.source));
            if (!status.log.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Build log")) EditorPlatform::Get().OpenFile(file::u8path(status.log));
            }
        }
    }

    const auto* viewport = ImGui::GetWindowViewport();
    ImGui::SetNextWindowSize({ImMin(editor::ThemePixels(340.f), viewport->WorkSize.x - editor::ThemePixels(16.f)),
        ImMin(editor::ThemePixels(400.f), viewport->WorkSize.y * .8f)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(editor::ThemePixels(8.f), editor::ThemePixels(6.f)));
    const bool popupOpen = ImGui::BeginPopup("AddComponent");
    ImGui::PopStyleVar();
    if (!popupOpen) return;
    if (target != m_addComponentTarget || EditorObjectOperations::IsEditLocked(entity, true))
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    bool resetResultsScroll = ImGui::IsWindowAppearing();
    if (m_newScriptPage || !m_componentCategory.empty())
    {
        if (ImGui::SmallButton(EditorIcon::Back))
        {
            m_newScriptPage = false;
            m_componentCategory.clear();
            m_componentSelection = 0;
            m_componentError.clear();
            resetResultsScroll = true;
        }
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(m_newScriptPage ? "New Script" : !SearchKey(m_componentSearch).empty() ? "Search Results"
        : m_componentCategory.empty() ? "Add Component" : m_componentCategory.c_str());
    ImGui::Separator();
    if (m_newScriptPage)
    {
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (m_focusNewScriptName) { ImGui::SetKeyboardFocusHere(); m_focusNewScriptName = false; }
        ImGui::InputTextWithHint("##NewScriptName", "PlayerController", &m_newScriptName);
        ImGui::TextDisabled("C# component");
        ImGui::TextWrapped("Assets/Script/%s.cs", m_newScriptName.empty() ? "<Name>" : m_newScriptName.c_str());
        ImGui::TextWrapped("Create the script, compile it, then add it to this entity.");
        ImGui::BeginDisabled(status.busy || SceneManagers->IsGameStart());
        if (ImGui::Button("Create and Add", {-FLT_MIN, 0}))
        {
            const auto result = EditorScriptAuthoring::CreateAndAttach(target, m_newScriptName);
            if (result.IsSuccess()) ImGui::CloseCurrentPopup();
            else m_componentError = result.message;
        }
        ImGui::EndDisabled();
        if (SceneManagers->IsGameStart()) ImGui::TextWrapped("Stop Play to create a script.");
        if (!m_componentError.empty()) ImGui::TextWrapped("%s", m_componentError.c_str());
        ImGui::EndPopup();
        return;
    }

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##ComponentSearch", "Search components and scripts...", &m_componentSearch))
    {
        m_componentSelection = 0;
        resetResultsScroll = true;
    }
    // Browse results while keeping the search field ready for further typing.
    ImGui::SetItemKeyOwner(ImGuiKey_UpArrow);
    ImGui::SetItemKeyOwner(ImGuiKey_DownArrow);
    const bool searching = !SearchKey(m_componentSearch).empty();
    const bool categoryPage = !searching && m_componentCategory.empty();
    struct Row { std::string title; const Entry* entry{}; };
    std::vector<Row> rows;
    if (categoryPage)
    {
        for (const auto& entry : m_componentCatalog)
            if (std::ranges::none_of(rows, [&](const Row& row) { return row.title == entry.category; }))
                rows.push_back({entry.category, nullptr});
        if (std::ranges::none_of(rows, [](const Row& row) { return row.title == "Scripts"; })) rows.push_back({"Scripts", nullptr});
        std::ranges::sort(rows, {}, &Row::title);
    }
    else
        for (const auto& entry : m_componentCatalog)
            if ((searching && Matches(entry, m_componentSearch)) || (!searching && entry.category == m_componentCategory))
                rows.push_back({entry.label, &entry});
    m_componentSelection = std::clamp(m_componentSelection, 0, ImMax(0, static_cast<int>(rows.size()) - 1));
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool moveSelection = false;
    if (focused && !rows.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { m_componentSelection = (m_componentSelection + 1) % rows.size(); moveSelection = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { m_componentSelection = (m_componentSelection + rows.size() - 1) % rows.size(); moveSelection = true; }
    }
    const bool enter = focused && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    std::string openCategory;
    const Entry* chosen = nullptr;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float errorHeight = m_componentError.empty() ? 0.f :
        ImGui::CalcTextSize(m_componentError.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + spacing;
    // Reserve the child end gap, separator gap and button without overflowing the popup.
    const float footer = ImGui::GetFrameHeightWithSpacing() + 2.f * spacing + errorHeight;
    ImGui::PushStyleColor(ImGuiCol_Header, editor::ThemeColorValue(editor::ThemeColor::Selection));
    if (ImGui::BeginChild("ComponentResults", {0, ImMax(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y - footer)},
        ImGuiChildFlags_None))
    {
        if (resetResultsScroll) ImGui::SetScrollY(0.f);
        if (rows.empty()) ImGui::TextWrapped("%s", m_componentCategory == "Scripts" && !searching
            ? "No compiled C# components. Create a script below or resolve compilation errors." : "No matching components.");
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            const auto& row = rows[i];
            bool attached = false;
            if (row.entry && !row.entry->managed)
            {
                const auto found = ComponentFactorys->m_componentTypes.find(row.entry->type);
                if (found != ComponentFactorys->m_componentTypes.end())
                    for (const auto& component : entity->m_components)
                        if (component && !component->IsDestroyMark() && component->GetTypeID() == Meta::TypeIDOf(*found->second)) attached = true;
            }
            ImGui::PushID(i);
            ImGui::BeginDisabled(attached);
            const auto rowLabel = row.entry ? row.title + (row.entry->managed ? " (Script)" : attached ? " (Added)" : "")
                : row.title;
            const bool clicked = ImGui::Selectable("##ComponentRow", i == m_componentSelection, ImGuiSelectableFlags_DontClosePopups,
                {0, ImGui::GetFrameHeight()});
            draw_component_menu_row(row.entry ? component_entry_visual(*row.entry) : component_category_visual(row.title),
                rowLabel.c_str(), row.entry == nullptr);
            if (clicked || (!attached && enter && i == m_componentSelection))
            {
                if (row.entry) chosen = row.entry;
                else openCategory = row.title;
            }
            ImGui::EndDisabled();
            if (moveSelection && i == m_componentSelection) ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && row.entry)
                ImGui::SetTooltip("%s\n%s%s", row.entry->type.c_str(), row.entry->category.c_str(), attached ? " - already added" : "");
            ImGui::PopID();
        }
        if (!openCategory.empty()) ImGui::SetScrollY(0.f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (!openCategory.empty()) { m_componentCategory = openCategory; m_componentSelection = 0; }
    if (chosen)
    {
        const auto result = chosen->managed ? EditorObjectOperations::AddManagedScript(target, chosen->type)
            : EditorObjectOperations::AddComponent(target, chosen->type);
        if (result.IsSuccess()) ImGui::CloseCurrentPopup();
        else m_componentError = result.message;
    }
    ImGui::Separator();
    if (ImGui::Button(EditorIcon::Label<EditorIcon::Script, " New Script...">, {-FLT_MIN, 0}))
    {
        m_newScriptPage = true;
        m_focusNewScriptName = true;
        m_newScriptName = m_componentSearch;
        m_componentError.clear();
    }
    if (!m_componentError.empty()) ImGui::TextWrapped("%s", m_componentError.c_str());
    ImGui::EndPopup();
}

void InspectorWindow::DrawManagedScripts(ScriptComponent* script)
{
	if (nullptr == script) return;

	auto& clr = ClrHost::Get();
	using namespace editor::widgets;
	// Script metadata changes only on reload. Use the same fixed label column and
	// narrow-width policy as native properties, independent of current field values.
	const auto layout = measure_property_layout(property_layout_inputs_now(0), m_layout);
	ImGui::SetNextItemWidth(begin_property_line("Script", layout));
	std::string scriptName = script->m_scriptType.empty() ? "Missing (Script)" : script->m_scriptType;
	ImGui::InputText("##ScriptReference", &scriptName, ImGuiInputTextFlags_ReadOnly);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", scriptName.c_str());

	// 타입 미지정 상태(구 씬에서 온 빈 컴포넌트 등)에서도 여기서 바로 고를 수 있게 한다.
	if (script->m_scriptType.empty())
	{
		ImGui::TextDisabled("스크립트 타입이 지정되지 않았습니다");

		const auto typeNames = clr.GetComponentTypeNames();
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

					// 인스턴스만 만든다 — 생명주기 훅은 재생에서 온다
					// (ScriptComponent::EnsureInstance의 주석). 편집 모드에
					// 필드를 그리려면 인스턴스가 필요하다.
					script->RetryInstance();
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
			// 실패 기억을 지우고 다시 시도한다. OnInitialized를 부르면 안 된다 —
			// 그것은 생명주기 전달까지 하는 창구라 편집 모드에서 훅이 새어 나간다.
			script->RetryInstance();
		}
		return;
	}

	const int instanceId = script->GetInstanceId();
	const int fieldCount = clr.GetFieldCount(instanceId);
	if (0 == fieldCount)
	{
		ImGui::TextDisabled("No exposed fields.");
		return;
	}

	// 인스턴스 id로 스코프를 묶어야 같은 이름 필드가 여러 스크립트에 있어도 위젯이 섞이지 않는다.
	ImGui::PushID(instanceId);

	for (int i = 0; i < fieldCount; ++i)
	{
		const std::string name = clr.GetFieldName(instanceId, i);
		ImGui::PushID(i);
		const float valueWidth = begin_property_line(name.c_str(), layout);
		ImGui::SetNextItemWidth(valueWidth);

		switch (clr.GetFieldType(instanceId, i))
		{
		case ClrHost::ScriptFieldType::Float:
		{
			float value = clr.GetFieldFloat(instanceId, i);
			if (drag_property_float("##Value", &value, 0.01f))
			{
				clr.SetFieldFloat(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Int32:
		{
			int value = clr.GetFieldInt32(instanceId, i);
			if (ImGui::DragInt("##Value", &value))
			{
				clr.SetFieldInt32(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Bool:
		{
			bool value = clr.GetFieldBool(instanceId, i);
			if (ImGui::Checkbox("##Value", &value))
			{
				clr.SetFieldBool(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Float3:
		{
			ClrHost::ScriptFloat3 value = clr.GetFieldFloat3(instanceId, i);
			axis_field3_request axes{};
			axes.label = "##Value";
			axes.values = &value.x;
			axes.speed = .01f;
			axes.stacked = layout.axis_stacked;
			if (draw_axis_field3(axes))
			{
				clr.SetFieldFloat3(instanceId, i, value);
				script->CaptureFields();
			}
			break;
		}
        case ClrHost::ScriptFieldType::AssetLink:
        {
            std::string value = clr.GetFieldString(instanceId, i);
            if (ImGui::InputText("##Value", &value, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (clr.SetFieldAssetLink(instanceId, i, value))
                {
                    script->CaptureFields();
                }
                else
                {
                    Debug::PrintLog(spdlog::level::warn, "[ScriptCore] Invalid AssetLink; expected 1:3:<asset UUID>:<subasset UUID>.");
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Texture link: 1:3:<asset UUID>:<subasset UUID>. Press Enter to apply. Nil/nil clears the link.");
            }
            break;
        }
		case ClrHost::ScriptFieldType::String:
		{
			std::string value = clr.GetFieldString(instanceId, i);

			if (ImGui::InputText("##Value", &value))
			{
				clr.SetFieldString(instanceId, i, value.c_str());
				script->CaptureFields();
			}
			break;
		}
		case ClrHost::ScriptFieldType::Object:
		{
			Entity* target = clr.GetFieldObject(instanceId, i);
			const std::string label = (nullptr != target) ? target->m_name.ToString() : std::string("None (Entity)");
			const float clearWidth = ImGui::GetFrameHeight();
			const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::Button((label + "###ObjectValue").c_str(), ImVec2(ImMax(1.f, valueWidth - clearWidth - gap), 0.f));
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDrag an entity from Hierarchy", label.c_str());

			// 계층 창에서 끌어다 놓는 것을 받는다. 페이로드 이름은 기존 드래그 소스와 맞춘다.
			if (!(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) && ImGui::BeginDragDropTarget())
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

			ImGui::SameLine(0.f, gap);
			ImGui::BeginDisabled(target == nullptr);
			if (ImGui::Button(EditorIcon::Label<EditorIcon::Close, "##ClearReference">, {clearWidth,clearWidth}))
			{
				clr.SetFieldObject(instanceId, i, nullptr);
				script->CaptureFields();
			}
			ImGui::EndDisabled();
			break;
		}
		default:
			ImGui::TextDisabled("Unsupported field type");
			break;
		}

		ImGui::PopID();
	}

	ImGui::PopID();
}

// 인스펙터 상단 구간 — 기본 정보와 공간 컴포넌트 — 이 **함께** 쓰는 라벨 열의
// 기준이다. 한 목록에서 폭을 뽑아 두 구간이 같은 열에 선다(계획서 계약 2 의
// "같은 깊이의 속성은 같은 열에 맞춘다").
//
// 목록에 든 것은 전부 컴파일 시 정해진 문자열이다. 매 프레임 바뀌는 값으로
// 재면 계획서가 금지한 "라벨 최대값 변화로 열이 흔들리는" 상태가 된다.
static const char* const kInspectorTopLabels[]{
    "Physics Layer", "Position", "Rotation", "Scale" };

static float InspectorTopLabelHint()
{
    return editor::widgets::property_layout_label_hint(
        kInspectorTopLabels, IM_ARRAYSIZE(kInspectorTopLabels));
}

Entity* InspectorWindow::DrawNavigation(Scene* scene, Entity* selected)
{
    auto& history = EditorSessionState::Get().SelectionHistory();
    history.Observe(scene ? scene->GetSceneId() : 0,
        selected ? scene->HandleOf(selected->m_index) : EntityHandle{});
    const auto alive = [scene](EntityHandle handle) {
        auto* entity = scene ? scene->Resolve(handle) : nullptr;
        return entity && !entity->IsDestroyMark();
    };
    const float buttonSize = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    for (int direction : {-1, 1})
    {
        if (direction == 1) ImGui::SameLine();
        const auto target = history.Peek(direction, alive);
        ImGui::BeginDisabled(!target.IsValid());
        if (ImGui::Button(direction < 0 ? EditorIcon::Label<EditorIcon::Back, "##SelectionBack">
            : EditorIcon::Label<EditorIcon::Forward, "##SelectionForward">, ImVec2(buttonSize, buttonSize)))
        {
            // Navigation is UI history, not an authoring Undo transaction.
            EditorObjectOperations::NavigateSelection(scene, direction);
            selected = scene->m_selectedEntity;
            ContentsBrowserWindow::selectedFileMetaNode.reset();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            auto* entry = scene ? scene->Resolve(target) : nullptr;
            ImGui::SetTooltip("%s%s%s", direction < 0 ? "Previous selection" : "Next selection",
                entry ? ": " : "", entry ? entry->m_name.ToString().c_str() : "");
        }
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.f, ImGui::GetContentRegionAvail().x - buttonSize));
    const bool inherited = selected && EditorObjectOperations::IsEditLocked(
        scene->TryGetEntity(selected->GetParentIndex()));
    const bool descendantsLocked = selected && !selected->m_editorLocked && !inherited &&
        EditorObjectOperations::IsEditLocked(selected, true);
    const bool locked = selected && (selected->m_editorLocked || inherited || descendantsLocked);
    ImGui::BeginDisabled(!selected || inherited || descendantsLocked);
    if (locked) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.91f,.72f,.38f,1));
    if (ImGui::Button(locked ? EditorIcon::Label<EditorIcon::Lock, "##EditLock">
        : EditorIcon::Label<EditorIcon::Unlock, "##EditLock">, ImVec2(buttonSize,buttonSize)))
        EditorObjectOperations::SetEditLocked(scene->HandleOf(selected->m_index), !selected->m_editorLocked);
    if (locked) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(inherited ? "Locked by parent. Unlock the parent first."
            : descendantsLocked ? "Contains a locked child. Unlock the child first."
            : locked ? "Unlock entity editing" : "Lock entity editing (Inspector, gizmo and hierarchy)");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::Separator();
    return selected;
}

void InspectorWindow::DrawEntityIcon(Entity* entity)
{
    auto& images = EditorAssetPresentation::Get();
    const float size = editor::ThemePixels(40.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    auto* texture = images.GetEntityIcon(entity->m_editorIcon);
    const bool clicked = texture ? ImGui::ImageButton("##EntityIcon", EditorImGuiTexture::From(texture), ImVec2(size,size))
        : ImGui::Button(EditorIcon::Label<EditorIcon::GameObject,"##EntityIcon">, ImVec2(size,size));
    if (clicked) ImGui::OpenPopup("##EntityIconPresets");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose entity icon");
    if (ImGui::BeginPopup("##EntityIconPresets"))
    {
        ImGui::TextDisabled("Entity icon");
        ImGui::Separator();
        const size_t current = editor::EntityIconIndex(entity->m_editorIcon);
        for (size_t i = 0; i < editor::EntityIconPresets.size(); ++i)
        {
            const auto& preset = editor::EntityIconPresets[i];
            ImGui::PushID(static_cast<int>(i));
            const auto start = ImGui::GetCursorScreenPos();
            const float rowHeight = ImGui::GetFrameHeight();
            const float imageSize = ImGui::GetFontSize();
            ImGui::Dummy(ImVec2(imageSize, rowHeight));
            if (auto* icon = images.GetEntityIcon(preset.id))
            {
                const ImVec2 min{start.x, start.y + (rowHeight-imageSize)*.5f};
                ImGui::GetWindowDrawList()->AddImage(EditorImGuiTexture::From(icon), min, ImVec2(min.x+imageSize,min.y+imageSize));
            }
            ImGui::SameLine();
            if (ImGui::MenuItem(preset.label, nullptr, i == current))
                EditorObjectOperations::SetIcon(entity->GetScene()->HandleOf(entity->m_index), std::string(preset.id));
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
}

void InspectorWindow::ImGuiDrawHelperGameObjectBaseInfo(Entity* gameObject)
{
	if (!ImGui::BeginTable("##EntityHeader", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) return;
	ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, editor::ThemePixels(46.f));
	ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableNextColumn();
	DrawEntityIcon(gameObject);
	ImGui::TableNextColumn();
	std::string name = gameObject->m_name.ToString();
	bool isEnabled = gameObject->IsEnabled();
	ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.65f, 0.78f, 0.47f, 1.f));
	ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, ImVec4(0.23f, 0.29f, 0.16f, 1.f));
	if (ImGui::Checkbox("##Enabled", &isEnabled))
		EditorObjectOperations::SetEntityEnabled(gameObject->GetScene()->HandleOf(gameObject->m_index), isEnabled);
	ImGui::PopStyleColor(2);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Object enabled");
    ImGui::SameLine();

    auto& tags = TagManagers->GetTags();
    const auto projectLayers = SceneManagers->ProjectLayers();
    const auto layerSnapshot = projectLayers ? projectLayers->Snapshot() : nullptr;
    auto& selectedTag = gameObject->m_tag;
    const auto* selectedLayer = layerSnapshot ? layerSnapshot->catalog.Find(gameObject->GetLayer()) : nullptr;
    const auto assignTag = [&](const std::string& tag) {
        TagManagers->RemoveTagFromObject(selectedTag.ToString(), gameObject);
        selectedTag = tag;
		TagManagers->AddTagToObject(selectedTag.ToString(), gameObject);
    };

    // The tag button occupies the trailing part of the name field. Reserve that
	// space outside InputText so long names cannot draw underneath the icon.
	const ImGuiStyle& baseStyle = ImGui::GetStyle();
	const float fieldHeight = ImGui::GetFrameHeight();
	const float staticWidth = fieldHeight + baseStyle.ItemInnerSpacing.x + ImGui::CalcTextSize("Static").x;
	const float available = ImGui::GetContentRegionAvail().x;
	const bool staticInline = available >= editor::ThemePixels(90.f) + fieldHeight +
		staticWidth + baseStyle.ItemSpacing.x;
	const float nameWidth = ImMax(available - (staticInline ? staticWidth + baseStyle.ItemSpacing.x : 0.f),
		fieldHeight + 1.f);
	const ImVec2 nameMin = ImGui::GetCursorScreenPos();
	const ImVec2 nameMax{nameMin.x + nameWidth, nameMin.y + fieldHeight};
	ImGui::GetWindowDrawList()->AddRectFilled(nameMin, nameMax, ImGui::GetColorU32(ImGuiCol_FrameBg),
		baseStyle.FrameRounding);
	ImGui::GetWindowDrawList()->AddRect(nameMin, nameMax, ImGui::GetColorU32(ImGuiCol_Border),
		baseStyle.FrameRounding);
	ImGui::BeginGroup();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
	ImGui::SetNextItemWidth(nameWidth - fieldHeight);

	if (ImGui::InputText("##name",
		&name[0],
		name.capacity() + 1,
		ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
		Meta::InputTextCallback,
		static_cast<void*>(&name)))
	{
		EditorObjectOperations::Rename(gameObject->GetScene()->HandleOf(gameObject->m_index), name);
	}
	ImGui::PopStyleColor();
	ImGui::SameLine(0.f, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, baseStyle.FramePadding.y));
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, editor::ThemeColorValue(editor::ThemeColor::Primary));
	if (ImGui::Button(EditorIcon::Label<EditorIcon::Tag, "##EntityTag">, ImVec2(fieldHeight, fieldHeight)))
		ImGui::OpenPopup("##EntityTagPicker");
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tag: %s", selectedTag.ToString().c_str());
	ImGui::PopStyleVar();
	ImGui::EndGroup();

	if (staticInline) ImGui::SameLine();
	ImGui::Checkbox("Static", &gameObject->m_isStatic);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(editor::ThemePixels(8.f), editor::ThemePixels(6.f)));
	if (ImGui::BeginPopup("##EntityTagPicker"))
	{
		for (const auto& tag : tags)
		{
			const bool isSelected = (selectedTag == tag);
			if (ImGui::MenuItem(tag.c_str(), nullptr, isSelected)) assignTag(tag);
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Add Tag")) m_openNewTagPopup = true;
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

    const editor::widgets::property_layout_metrics baseLayout = editor::widgets::measure_property_layout(
        editor::widgets::property_layout_inputs_now(0, InspectorTopLabelHint()), m_layout);
    ImGui::SetNextItemWidth(editor::widgets::begin_property_line("Layer", baseLayout));
    if (ImGui::BeginCombo("##LayerCombo", selectedLayer ? selectedLayer->name.c_str() : "Invalid layer"))
    {
        if (layerSnapshot)
            for (const auto& layer : layerSnapshot->catalog.definitions)
            {
                if (!layer || layer->retired)
                    continue;
                const bool selected = gameObject->GetLayer() == layer->id;
                if (ImGui::Selectable(layer->name.c_str(), selected))
                {
                    const auto result = EditorObjectOperations::SetEntityLayer(
                        gameObject->GetScene()->HandleOf(gameObject->m_index), layer->id);
                    if (!result.IsSuccess())
                        Debug::PrintLog(spdlog::level::err, result.message);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        if (ImGui::Selectable("Add Layer"))
            m_openNewLayerPopup = true;
        ImGui::EndCombo();
    }

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
				const auto tagResult = EditorProjectOperations::AddTag(newTagName);
				if (tagResult.IsSuccess()) assignTag(std::string(newTagName));
				else Debug::PrintLog(spdlog::level::err, tagResult.message);
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
                const auto result = EditorProjectOperations::AddLayer(newLayerName);
                if (result.IsSuccess())
                {
                    const auto snapshot = projectLayers->Snapshot();
                    const auto* added = snapshot->catalog.Find(newLayerName);
                    if (added)
                        EditorObjectOperations::SetEntityLayer(gameObject->GetScene()->HandleOf(gameObject->m_index),
                                                               added->id);
                }
                else
                    Debug::PrintLog(spdlog::level::err, result.message);
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
    ImGui::EndTable();
}

// 트랜스폼 세 줄. 라벨과 값 열은 공통 배치 계층이 놓고(W2-I2) 이 함수는 값만
// 그린다 — 그래서 `##` 이름을 넘겨 위젯 쪽 라벨을 끈다.
//
// 예전에는 호출자가 `Text("Position ")` 처럼 **공백으로 자리를 맞췄다.**
// "Scale" 뒤에 공백 다섯을 붙여 "Position" 과 폭을 맞추는 식이었고, 폰트나
// 배율이 바뀌면 그대로 어긋난다. 그 자리를 계산된 라벨 열이 대신한다.
static bool DrawTransformAxes(const char* label, float* values,
    float speed, float min, float max,
    const editor::widgets::property_layout_metrics& metrics)
{
    ImGui::SetNextItemWidth(metrics.value_col);

    editor::widgets::axis_field3_request axes{};
    axes.label = label;
    axes.values = values;
    axes.speed = speed;
    axes.min = min;
    axes.max = max;
    axes.stacked = metrics.axis_stacked;
    return editor::widgets::draw_axis_field3(axes);
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

	// 본문만 그린다(W2-I1 · 2안). 머리줄·체크박스·메뉴는 인스펙터의 공통 순회가 소유한다 —
	// 예전에는 이 함수가 자기 패널을 열어, 공통 순회 밖에서 한 번 더 도는 두 번째 길이었다.
	{
		// 이 컴포넌트의 세 줄이 같은 배치를 쓴다. 줄마다 다시 재면 라벨 길이가
		// 다른 줄끼리 값 열이 어긋난다.
		const editor::widgets::property_layout_metrics layout =
			editor::widgets::measure_property_layout(
				editor::widgets::property_layout_inputs_now(0, InspectorTopLabelHint()),
				m_layout);

		editor::widgets::begin_property_line("Position", layout);
		const math::vector4 positionBeforeEdit = position;
		if (DrawTransformAxes("##Position", &position.x, 0.08f, -1000.f, 1000.f, layout))
		{
			if (!editingPosition)
			{
				prevPosition = positionBeforeEdit;
				editingPosition = true;
			}
			gameObject->Transform_().SetPositionValue(
				position, TransformWriteReason::Inspector);
		}
		if (editingPosition && ImGui::IsItemDeactivatedAfterEdit())
		{
			if (prevPosition != position)
			{
				gameObject->Transform_().SetPositionValue(prevPosition, TransformWriteReason::Inspector);
                EditorObjectOperations::Transform(gameObject->GetScene()->HandleOf(gameObject->m_index), math::vector3{position.x, position.y, position.z}, gameObject->Transform_().GetRotation(), gameObject->Transform_().GetScale());
			}
			editingPosition = false;
		}

		static bool editingRotation = false;
		static math::vector4 prevRotation{};

		const math::vector3 currentEuler = math::to_euler(math::quaternion{
			rotation.x, rotation.y, rotation.z, rotation.w });
		float pyr[3]{ currentEuler.x, currentEuler.y, currentEuler.z };
		float deltaEuler[3] = { 0, 0, 0 };

		float prevPYR[3];

		for (float& i : pyr) i *= math::rad_to_deg;
		prevPYR[0] = pyr[0];
		prevPYR[1] = pyr[1];
		prevPYR[2] = pyr[2];

		editor::widgets::begin_property_line("Rotation", layout);
		if (DrawTransformAxes("##Rotation", pyr, 0.1f, 0.f, 0.f, layout))
		{
			if (!editingRotation)
			{
				prevRotation = rotation;
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
			if (prevRotation != rotation)
			{
				gameObject->Transform_().SetRotationValue(prevRotation, TransformWriteReason::Inspector);
                EditorObjectOperations::Transform(gameObject->GetScene()->HandleOf(gameObject->m_index), gameObject->Transform_().GetPosition(), math::quaternion{rotation.x, rotation.y, rotation.z, rotation.w}, gameObject->Transform_().GetScale());
			}
			editingRotation = false;
		}

		static bool editingScale = false;
		static math::vector4 prevScale{};

		editor::widgets::begin_property_line("Scale", layout);
		const math::vector4 scaleBeforeEdit = scale;
		if (DrawTransformAxes("##Scale", &scale.x, 0.1f, 0.001f, 1000.f, layout))
		{
			if (!editingScale)
			{
				prevScale = scaleBeforeEdit;
				editingScale = true;
			}
			gameObject->Transform_().SetScaleValue(
				scale, TransformWriteReason::Inspector);
		}
		if (editingScale && ImGui::IsItemDeactivatedAfterEdit())
		{
			if (prevScale != scale)
			{
				gameObject->Transform_().SetScaleValue(prevScale, TransformWriteReason::Inspector);
                EditorObjectOperations::Transform(gameObject->GetScene()->HandleOf(gameObject->m_index), gameObject->Transform_().GetPosition(), gameObject->Transform_().GetRotation(), math::vector3{scale.x, scale.y, scale.z});
			}
			editingScale = false;
		}

		{
			gameObject->Transform_().UpdateLocalMatrix();
		}
	}

}

// 컴포넌트 메뉴의 "Reset Transform". 예전에는 Transform 드로어가 자기 팝업으로 들고 있었다.
static void ResetTransform(Entity* gameObject)
{
	gameObject->Transform_().SetPositionValue(
		{ 0.f, 0.f, 0.f, 1.f }, TransformWriteReason::Inspector);
	gameObject->Transform_().SetRotationValue(
		{ 0.f, 0.f, 0.f, 1.f }, TransformWriteReason::Inspector);
	gameObject->Transform_().SetScaleValue(
		{ 1.f, 1.f, 1.f, 1.f }, TransformWriteReason::Inspector);
	gameObject->Transform_().UpdateLocalMatrix();
}

// ── 전용 드로어의 공통 조각 (PHASE 21 W2-I4) ───────────────────────────────
//
// 아래 드로어들은 `editor::widgets::property_sheet` 로 라벨 열을 한 번 재고, 값 칸은
// 그 줄이 돌려준 폭을 쓴다. 고정 픽셀 폭은 두지 않는다 — 배율을 받지 않고 좁은
// 폭에서 넘친다(착수 기준선: ImageComponent 240 에서 260 px).

static bool DropTargetEnabled()
{
	return !(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled);
}

// 자산·대상 이름을 보여 주는 버튼. 이름은 왼쪽 정렬한다(MeshRenderer 의 Element 와 같다).
static bool NameButton(const std::string& name, const char* id, float width)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
	const bool pressed = ImGui::Button((name + id).c_str(), ImVec2(width, 0.f));
	ImGui::PopStyleVar();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
	return pressed;
}

// 자산 칸: 이름을 보여 주는 버튼 하나. 버튼이 끌어 놓기 대상이다. 텍스처가 있으면
// 앞에 줄 높이의 미리보기를 붙인다. 놓인 자산 경로를 돌려준다(없으면 빈 경로).
static file::path DrawAssetSlot(const char* id, const own::shared_owner<const Texture>& texture, const char* emptyText,
	const char* payloadType, float width)
{
	ImGui::PushID(id);
	float buttonWidth = width;
	if (texture)
	{
		const float preview = ImGui::GetFrameHeight();
		const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
		ImGui::Image((ImTextureID)EditorImGuiTexture::From(texture), ImVec2(preview, preview));
		ImGui::SameLine(0.f, gap);
		buttonWidth = ImMax(1.f, width - preview - gap);
	}
	const std::string name = texture ? texture->m_name + texture->m_extension : std::string(emptyText);
	NameButton(name, "###Slot", buttonWidth);

	file::path dropped;
	if (DropTargetEnabled() && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
			dropped = editor::asset_drag::path_of(*payload);
		ImGui::EndDragDropTarget();
	}
	ImGui::PopID();
	return dropped;
}

// 읽기 전용 이름 칸과 그 뒤의 고르기 버튼. 버튼을 눌렀으면 참.
static bool DrawNamedPicker(const editor::widgets::property_sheet& sheet, const char* label,
	const std::string& name, const char* emptyText, const char* buttonLabel)
{
	ImGui::PushID(label);
	ImGui::SetNextItemWidth(sheet.line_before_buttons(label));
	std::string shown = name.empty() ? std::string(emptyText) : name;
	ImGui::InputText("##Name", &shown, ImGuiInputTextFlags_ReadOnly);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shown.c_str());
	ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
	const float button = ImGui::GetFrameHeight();
	const bool pressed = ImGui::Button(buttonLabel, ImVec2(button, button));
	ImGui::PopID();
	return pressed;
}

void InspectorWindow::ImGuiDrawHelperFSM(StateMachineComponent* FSMComponent)
{
	if (!FSMComponent) return;

	const editor::widgets::property_sheet sheet(m_layout, { "State Machine" });
	if (ImGui::Button("Edit###EditStateMachine", ImVec2(sheet.line("State Machine"), 0.f)))
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

void InspectorWindow::ImGuiDrawHelperBT(BehaviorTreeComponent* BTComponent)
{
	if (!BTComponent) return;

	const editor::widgets::property_sheet sheet(m_layout, { "Behavior Tree", "BlackBoard" });
	const auto pick = [](const wchar_t* filter, const wchar_t* title, std::string& name,
		FileGuid& guid, const char* kind)
	{
		file::path filePath = ShowOpenFileDialog(filter, title,
			PathFinder::Relative("BehaviorTree").wstring());
		if (filePath.empty()) return;

		name = filePath.stem().string();
		const FileGuid found = DataSystems->GetFileGuid(filePath);
		if (found != nullFileGuid)
			guid = found;
		else
			Debug::PrintLog(spdlog::level::err, std::string("Failed to get file GUID for ") + kind + ": " + filePath.string());
	};

	if (DrawNamedPicker(sheet, "Behavior Tree", BTComponent->name, "None",
		EditorIcon::Label<EditorIcon::AssetPicker, "##PickBehaviorTree">))
	{
		pick(L"Behavior Tree Files (*.bt)\0*.bt\0", L"Load Behavior Tree",
			BTComponent->name, BTComponent->m_BehaviorTreeGuid, "Behavior Tree");
	}
	if (DrawNamedPicker(sheet, "BlackBoard", BTComponent->blackBoardName, "None",
		EditorIcon::Label<EditorIcon::AssetPicker, "##PickBlackBoard">))
	{
		pick(L"BlackBoard Files (*.blackboard)\0*.blackboard\0", L"Load BlackBoard",
			BTComponent->blackBoardName, BTComponent->m_BlackBoardGuid, "Blackboard");
	}
}

void InspectorWindow::ImGuiDrawHelperRenderProfile(SceneRenderProfileComponent* renderProfileComponent)
{
	if (!renderProfileComponent) return;

	{
		const editor::widgets::property_sheet sheet(m_layout, { "Profile" });
		const float width = sheet.line("Profile");
		const std::string name = renderProfileComponent->m_renderProfileName.empty()
			? std::string("None (drag Scene Render Profile)") : renderProfileComponent->m_renderProfileName;
		NameButton(name, "###SceneRenderProfileSlot", width);
		if (DropTargetEnabled() && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SceneRenderProfile"))
			{
				const file::path filepath = editor::asset_drag::path_of(*payload);
				FileGuid guid = DataSystems->GetFileGuid(filepath);
				if (guid != nullFileGuid)
				{
					// 이미 프로파일이 존재하는 경우
					if (renderProfileComponent->m_renderProfileGuid != nullFileGuid)
					{
						Debug::PrintLog(spdlog::level::warn, "Scene render profile already exists. Replacing with new profile.");
					}
					renderProfileComponent->m_renderProfileGuid = guid;
					renderProfileComponent->LoadProfile(guid);
				}
				else
				{
					Debug::PrintLog(spdlog::level::err, "Failed to load scene render profile: " + filepath.string());
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	if (!renderProfileComponent->IsProfileLoaded()) return;

	SceneRenderProfile& profile = renderProfileComponent->GetRenderProfile();
	const auto drawMembers = [](const char* header, auto& setting)
	{
		if (!ImGui::CollapsingHeader(header)) return;
		ImGui::PushID(header);
		Meta::TypedDraw::DrawOwnMembers(setting);
		ImGui::PopID();
	};

	drawMembers("ShadowPass", profile.settings.shadow);
	drawMembers("SSAOPass", profile.settings.ssao);
	drawMembers("DeferredPass", profile.settings.deferred);
	drawMembers("SSGIPass", profile.settings.ssgi);

	if (ImGui::CollapsingHeader("SkyBoxPass"))
	{
		const editor::widgets::property_sheet sheet(m_layout, { "Use SkyBox", "HDR" });
		sheet.line("Use SkyBox");
		ImGui::Checkbox("##UseSkyBox", &profile.settings.m_isSkyboxEnabled);

		const float width = sheet.line("HDR");
		const std::string hdrName = profile.settings.skyboxTextureName.empty()
			? std::string("None (drag HDR texture)") : profile.settings.skyboxTextureName;
		NameButton(hdrName, "###HdrSlot", width);
		if (DropTargetEnabled() && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HDR"))
			{
				const file::path filepath = editor::asset_drag::path_of(*payload);
				FileGuid guid = DataSystems->GetFileGuid(filepath);
				if (guid != nullFileGuid)
				{
					profile.settings.skyboxTextureName = filepath.filename().string();
				}
				else
				{
					Debug::PrintLog(spdlog::level::err, "Failed to load HDR: " + filepath.string());
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::Separator();
	if (ImGui::CollapsingHeader("PostProcessPass"))
	{
		drawMembers("AAPass", profile.settings.aa);
		drawMembers("BloomPass", profile.settings.bloom);
		drawMembers("VignettePass", profile.settings.vignette);

		if (ImGui::CollapsingHeader("ToneMapPass"))
		{
			auto& setting = profile.settings.toneMap;
			ImGui::PushID("ToneMapPass");

			const editor::widgets::property_sheet sheet(m_layout, { "Use ToneMap", "ToneMap Type",
				"Use Auto Exposure", "Exposure", "fNumber", "Shutter Time", "ISO",
				"Exposure Compensation", "Speed Brightness", "Speed Darkness" });
			sheet.line("Use ToneMap");
			ImGui::Checkbox("##UseToneMap", &setting.isAbleToneMap);
			ImGui::SetNextItemWidth(sheet.line("ToneMap Type"));
			ImGui::Combo("##ToneMapType", &setting.toneMapType, "Reinhard\0ACES\0Uncharted2\0HDR10\0ACESFlim");

			ImGui::SeparatorText("Auto Exposure");
			sheet.line("Use Auto Exposure");
			ImGui::Checkbox("##UseAutoExposure", &setting.isAbleAutoExposure);
			ImGuiSliderFlags exposureFlags = setting.isAbleAutoExposure ? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None;
			ImGui::SetNextItemWidth(sheet.line("Exposure"));
			ImGui::DragFloat("##ToneMapExposure", &setting.toneMapExposure, 0.01f, 0.0f, 5.0f, "%.3f", exposureFlags);

			ImGui::SeparatorText("Manual Camera");
			ImGui::SetNextItemWidth(sheet.line("fNumber"));
			ImGui::DragFloat("##fNumber", &setting.fNumber, 0.01f, 1.0f, 32.0f);
			ImGui::SetNextItemWidth(sheet.line("Shutter Time"));
			ImGui::DragFloat("##ShutterTime", &setting.shutterTime, 0.001f, 0.000125f, 30.0f);
			ImGui::SetNextItemWidth(sheet.line("ISO"));
			ImGui::DragFloat("##ISO", &setting.ISO, 50.0f, 50.0f, 6400.0f);
			ImGui::SetNextItemWidth(sheet.line("Exposure Compensation"));
			ImGui::DragFloat("##ExposureCompensation", &setting.exposureCompensation, 0.01f, -5.0f, 5.0f);
			ImGui::SetNextItemWidth(sheet.line("Speed Brightness"));
			ImGui::DragFloat("##SpeedBrightness", &setting.speedBrightness, 0.01f, 0.1f, 10.0f);
			ImGui::SetNextItemWidth(sheet.line("Speed Darkness"));
			ImGui::DragFloat("##SpeedDarkness", &setting.speedDarkness, 0.01f, 0.1f, 10.0f);

			ImGui::PopID();
		}

		drawMembers("ColorGradingPass", profile.settings.colorGrading);
	}

	renderProfileComponent->UpdateProfileEditMode();

	ImGui::Separator();
	if (ImGui::Button("Save Scene Render Profile Asset", ImVec2(ImGui::GetContentRegionAvail().x, 0.f)))
	{
		EditorAssetDatabase::Get().SaveExistingSceneRenderProfile(
			renderProfileComponent->m_renderProfileGuid, &profile);
	}
}

void InspectorWindow::ImGuiDrawHelperDecal(DecalComponent* decalComponent)
{
	const editor::widgets::property_sheet sheet(m_layout, { "Slice X", "Slice Y", "Slice Number",
		"Use Animation", "Slices Per Second", "Loop", "Diffuse", "Normal", "ORM" });

	int sliceX = decalComponent->sliceX;
	int sliceY = decalComponent->sliceY;
	ImGui::SetNextItemWidth(sheet.line("Slice X"));
	ImGui::SliderInt("##SliceX", &sliceX, 1, 20);
	ImGui::SetNextItemWidth(sheet.line("Slice Y"));
	ImGui::SliderInt("##SliceY", &sliceY, 1, 20);
	ImGui::SetNextItemWidth(sheet.line("Slice Number"));
	ImGui::InputInt("##SliceNumber", &decalComponent->sliceNumber);
	decalComponent->sliceX = sliceX;
	decalComponent->sliceY = sliceY;

	sheet.line("Use Animation");
	ImGui::Checkbox("##UseAnimation", &decalComponent->useAnimation);
	if (decalComponent->useAnimation)
	{
		ImGui::SetNextItemWidth(sheet.line("Slices Per Second"));
		ImGui::SliderFloat("##SlicePerSeconds", &decalComponent->slicePerSeconds, 0.f, 10.f, "%.5f");
		sheet.line("Loop");
		ImGui::Checkbox("##Loop", &decalComponent->isLoop);
	}

	// 데칼은 이름만 저장하고 Textures 에서 다시 찾는다(DecalComponent.cpp).
	const auto slot = [&](const char* label, const own::shared_owner<const Texture>& texture, const char* context)
	{
		const file::path dropped = DrawAssetSlot(label, texture, "None", "Texture", sheet.line(label));
		return !dropped.empty() && editor::asset_drag::lives_in(dropped, "Textures", context)
			? dropped.filename().string() : std::string();
	};
	if (auto name = slot("Diffuse", decalComponent->GetDecalTextureShared(), "Decal Decal texture drop"); !name.empty())
	{
		decalComponent->SetDecalTexture(name.c_str());
	}
	if (auto name = slot("Normal", decalComponent->GetNormalTextureShared(), "Decal Normal texture drop"); !name.empty())
	{
		decalComponent->SetNormalTexture(name.c_str());
	}
	if (auto name = slot("ORM", decalComponent->GetORMTextureShared(), "Decal ORM texture drop"); !name.empty())
	{
		decalComponent->SetORMTexture(name.c_str());
	}
}

void InspectorWindow::ImGuiDrawHelperImageComponent(ImageComponent* imageComponent)
{
	const editor::widgets::property_sheet sheet(m_layout, { "Image", "Add Texture", "Color Tint",
		"Rotation", "Origin", "Union Scale", "Layer", "Size", "Clip Direction", "Clip Percent",
		"Left", "Right", "Up", "Down" });

	const auto& textures = imageComponent->GetTextures();
	const int count = static_cast<int>(textures.size());
	if (count > 0)
	{
		// 컴포넌트가 자기 인덱스를 들고 있다. 창에 정적 인덱스를 두면 이미지
		// 컴포넌트 둘이 한 값을 나눠 쓴다.
		const int current = ImClamp(imageComponent->curindex, 0, count - 1);
		ImGui::SetNextItemWidth(sheet.line("Image"));
		if (ImGui::BeginCombo("##TextureCombo", textures[current]->m_name.c_str()))
		{
			for (int i = 0; i < count; ++i)
			{
				ImGui::PushID(i);
				const bool isSelected = (current == i);
				if (ImGui::Selectable(textures[i]->m_name.c_str(), isSelected))
					imageComponent->SetTexture(i);
				if (isSelected)
					ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	const file::path dropped = DrawAssetSlot("AddTexture", {},
		count > 0 ? "Drag UI texture to add" : "No textures - drag UI texture", "UI_TEXTURE",
		sheet.line("Add Texture"));
	if (!dropped.empty())
	{
		auto texture = DataSystems->LoadSharedTexture(dropped.string().c_str(),
			DataSystem::TextureFileType::UITexture);
		if (texture)
		{
			imageComponent->Load(texture);
			imageComponent->SetTexture(static_cast<int>(imageComponent->GetTextures().size() - 1));
		}
		else
		{
			Debug::PrintLog(spdlog::level::err, "Failed to load UI Texture: " + dropped.string());
		}
	}

	ImGui::SeparatorText("BaseInfo");
	ImGui::SetNextItemWidth(sheet.line("Color Tint"));
	ImGui::ColorEdit4("##ColorTint", &imageComponent->color.r);
	ImGui::SetNextItemWidth(sheet.line("Rotation"));
	ImGui::DragFloat("##Rotation", &imageComponent->rotate, 0.1f, -360.0f, 360.0f);
	ImGui::SetNextItemWidth(sheet.line("Origin"));
	ImGui::DragFloat2("##Origin", &imageComponent->origin.x, 0.01f, 0.0f, 1.0f);
	ImGui::SetNextItemWidth(sheet.line("Union Scale"));
	ImGui::DragFloat("##UnionScale", &imageComponent->unionScale, 0.01f, 1.f, 10.f);
	ImGui::SetNextItemWidth(sheet.line("Layer"));
	ImGui::InputInt("##Layer", &imageComponent->_layerorder);
	if (ImGui::Button("Reset###ResetSize", ImVec2(sheet.line("Size"), 0.f)))
	{
		imageComponent->ResetSize();
	}

	static const char* clipDirections[] = { "None", "LeftToRight", "RightToLeft", "UpToBottom", "BottomToTop" };
	int currentClipDir = static_cast<int>(imageComponent->clipDirection);
	ImGui::SetNextItemWidth(sheet.line("Clip Direction"));
	ImGui::Combo("##ClipDirection", &currentClipDir, clipDirections, IM_ARRAYSIZE(clipDirections));
	imageComponent->clipDirection = static_cast<ClipDirection>(currentClipDir);
	ImGui::SetNextItemWidth(sheet.line("Clip Percent"));
	ImGui::DragFloat("##ClipPercent", &imageComponent->clipPercent, 0.01f, 0.0f, 1.0f);

	ImGui::SeparatorText("Navigation");
	const auto navigationTarget = [&](const char* label, Direction direction)
	{
		ImGui::PushID(label);
		Entity* target = imageComponent->GetNextNavi(direction);
		const std::string name = target ? target->m_name.ToString() : std::string("None (drag object)");
		NameButton(name, "###Target", sheet.line(label));
		if (DropTargetEnabled() && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT"))
			{
				Entity::Index draggedIndex = *(Entity::Index*)payload->Data;
				if (draggedIndex != imageComponent->GetOwner()->m_index)
					imageComponent->SetNavi(direction, Entity::FindIndex(draggedIndex));
			}
			// 예전 판은 네 방향 모두 여기서 닫지 않았다.
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
	};
	navigationTarget("Left", Direction::Left);
	navigationTarget("Right", Direction::Right);
	navigationTarget("Up", Direction::Up);
	navigationTarget("Down", Direction::Down);
}

void InspectorWindow::ImGuiDrawHelperSpriteRenderer(SpriteRenderer* spriteRenderer)
{
	{
		const editor::widgets::property_sheet sheet(m_layout, { "Sprite" });
		const auto& sprite = spriteRenderer->GetSprite();
		const file::path dropped = DrawAssetSlot("Sprite", sprite, "None (drag texture)",
			"Texture", sheet.line("Sprite"));
		if (!dropped.empty())
		{
			auto texture = DataSystems->LoadSharedTexture(dropped.string().c_str(), DataSystem::TextureFileType::Texture);
			spriteRenderer->SetSprite(texture);
		}
	}

	if (const auto* type = Meta::Find(type_guid(SpriteRenderer)))
	{
		Meta::TypedDraw::DrawOwnMembers(*spriteRenderer);
	}
}

void InspectorWindow::ImGuiDrawHelperCanvas(Canvas* canvas)
{
	const editor::widgets::property_sheet sheet(m_layout, { "Canvas Name", "Canvas Order" });
	ImGui::SetNextItemWidth(sheet.line("Canvas Name"));
	ImGui::InputText("##CanvasName", &canvas->CanvasName);

	int order = canvas->CanvasOrder;
	ImGui::SetNextItemWidth(sheet.line("Canvas Order"));
	if (ImGui::DragInt("##CanvasOrder", &order))
	{
		canvas->SetCanvasOrder(order);
	}
}

void InspectorWindow::ImGuiDrawHelperSoundComponent(SoundComponent* sc)
{
    using namespace ImGui;
    sc->MarkPreviewVisible();
    auto settings = sc->ReadSettings();
    bool changed = false;
    const editor::widgets::property_sheet sheet(m_layout, { "Source", "Clip", "Asset GUID", "Bus",
        "Volume", "Pitch", "Priority", "Concurrency Group", "Same Clip", "Virtualization", "Loop", "Play On Start", "Spatial", "Spatial Blend",
        "Min Distance", "Max Distance", "Rolloff", "Reverb Send", "Reverb Level (dB)", "Preview" });

    const char* sources[] = { "Audio Clip", "Sound Preset", "Sound Graph" };
    int source = static_cast<int>(settings.sourceKind);
    SetNextItemWidth(sheet.line("Source"));
    if (Combo("##SoundSource", &source, sources, IM_ARRAYSIZE(sources)))
    {
        settings.sourceKind = static_cast<wave::SoundSourceKind>(source);
        changed = true;
    }
    if (settings.sourceKind == wave::SoundSourceKind::Clip)
    {
        if (DrawNamedPicker(sheet, "Clip", settings.clipKey, "None",
            EditorIcon::Label<EditorIcon::Audio, "##PickClip">))
        {
            m_clipKeyCache = sc->GetOwner()->GetScene()->Sounds().ClipKeys();
            m_clipSearch.clear();
            m_clipPickerTarget = sc;
            m_openClipPicker = true;
        }
    }
    else
    {
        auto& asset = settings.sourceKind == wave::SoundSourceKind::Preset ?
            settings.soundPresetKey : settings.soundGraphKey;
        SetNextItemWidth(sheet.line("Asset GUID"));
        changed |= InputText("##SoundAssetGuid", &asset);
    }
    if (settings.sourceKind == wave::SoundSourceKind::Preset)
    {
        changed |= Checkbox("Override preset settings", &settings.overridePresetSettings);
    }
    BeginDisabled(settings.sourceKind == wave::SoundSourceKind::Preset && !settings.overridePresetSettings);
    SeparatorText("Bus / Params");
    const char* buses[] = { "BGM", "SFX", "PLAYER", "MONSTER", "UI" };
    int bus = static_cast<int>(settings.bus);
    SetNextItemWidth(sheet.line("Bus"));
    if (Combo("##Bus", &bus, buses, IM_ARRAYSIZE(buses)))
    {
        settings.bus = static_cast<ChannelType>(bus);
        changed = true;
    }
    SetNextItemWidth(sheet.line("Volume"));
    changed |= DragFloat("##Volume", &settings.volume, 0.01f, 0.0f, 1.0f, "%.3f");
    SetNextItemWidth(sheet.line("Pitch"));
    changed |= DragFloat("##Pitch", &settings.pitch, 0.01f, 0.25f, 4.0f, "%.2f");
    SetNextItemWidth(sheet.line("Priority"));
    changed |= DragInt("##Priority", &settings.priority, 1, 0, 256);
    int concurrency = static_cast<int>(settings.concurrencyGroup);
    SetNextItemWidth(sheet.line("Concurrency Group"));
    if (DragInt("##ConcurrencyGroup", &concurrency, 1.0f, 0, 65535))
    {
        settings.concurrencyGroup = static_cast<std::uint32_t>(concurrency);
        changed = true;
    }
    sheet.line("Same Clip");
    changed |= Checkbox("Preempt same clip##Audio", &settings.preemptSameClip);
    sheet.line("Virtualization");
    changed |= Checkbox("Allow virtualization##Audio", &settings.allowVirtualization);
    sheet.line("Loop");
    changed |= Checkbox("##Loop", &settings.loop);
    sheet.line("Play On Start");
    changed |= Checkbox("##PlayOnStart", &settings.playOnStart);

    SeparatorText("Spatial");
    sheet.line("Spatial");
    changed |= Checkbox("##Spatial", &settings.spatial);
    if (settings.spatial)
    {
        SetNextItemWidth(sheet.line("Spatial Blend"));
        changed |= DragFloat("##SpatialBlend", &settings.spatialBlend, 0.01f, 0.0f, 1.0f, "%.2f");
        SetNextItemWidth(sheet.line("Min Distance"));
        changed |= DragFloat("##MinDistance", &settings.minDistance, 0.01f, 0.01f, 200.0f, "%.2f");
        SetNextItemWidth(sheet.line("Max Distance"));
        changed |= DragFloat("##MaxDistance", &settings.maxDistance, 0.1f, 0.1f, 500.0f, "%.2f");
        settings.maxDistance = std::max(settings.minDistance, settings.maxDistance);
        const char* rolloffs[] = { "Linear", "Inverse", "Custom" };
        int rolloff = static_cast<int>(settings.rolloff);
        SetNextItemWidth(sheet.line("Rolloff"));
        if (Combo("##Rolloff", &rolloff, rolloffs, IM_ARRAYSIZE(rolloffs)))
        {
            settings.rolloff = static_cast<Rolloff>(rolloff);
            changed = true;
        }
        SeparatorText("Distance Rolloff Curve");
        if (settings.rolloff == Rolloff::Custom)
        {
            if (settings.localRolloffCurve.size() < 2u)
            {
                settings.localRolloffCurve = { { 0.0f, 1.0f }, { settings.maxDistance, 0.0f } };
                changed = true;
            }
            changed |= DrawRolloffCurveEditor(settings.localRolloffCurve, settings.maxDistance,
                ImVec2(0.0f, editor::ThemePixels(200.0f)), nullptr, false);
        }
        else
        {
            std::vector<CurvePoint> curve;
            if (settings.rolloff == Rolloff::Linear)
            {
                BuildLinearCurve(curve, settings.minDistance, settings.maxDistance);
            }
            else
            {
                BuildInverseCurve(curve, settings.minDistance, settings.maxDistance);
            }
            DrawRolloffCurveEditor(curve, settings.maxDistance,
                ImVec2(0.0f, editor::ThemePixels(200.0f)), nullptr, true);
        }
    }
    SeparatorText("Room Reverb Send");
    sheet.line("Reverb Send");
    changed |= Checkbox("##EnableReverbSend", &settings.useReverbSend);
    SetNextItemWidth(sheet.line("Reverb Level (dB)"));
    changed |= DragFloat("##ReverbLevel", &settings.reverbLevel, 0.1f, -80.0f, 10.0f, "%.1f dB");
    EndDisabled();
    if (changed)
    {
        settings.reverbBus = "Room";
        sc->QueueSettings(std::move(settings));
    }
    Separator();
    const float gap = GetStyle().ItemInnerSpacing.x;
    const float button = ImMax(1.0f, (sheet.line("Preview") - gap * 2.0f) / 3.0f);
    if (Button("Play", ImVec2(button, 0.0f)))
    {
        sc->QueuePreview(SoundComponent::PreviewCommand::Play);
    }
    SameLine(0.0f, gap);
    if (Button("Stop", ImVec2(button, 0.0f)))
    {
        sc->QueuePreview(SoundComponent::PreviewCommand::Stop);
    }
    SameLine(0.0f, gap);
    if (Button("OneShot", ImVec2(button, 0.0f)))
    {
        sc->QueuePreview(SoundComponent::PreviewCommand::OneShot);
    }
}

bool InspectorWindow::DrawRolloffCurveEditor(std::vector<CurvePoint>& sourceCurve, float maxDist, ImVec2 size, int* outSelected, bool readOnly)
{
	using namespace ImGui;
    readOnly |= (GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    std::vector<CurvePoint> preview;
    if (readOnly) preview = sourceCurve;
    auto& curve = readOnly ? preview : sourceCurve;
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
    if (!m_openClipPicker || !m_clipPickerTarget)
    {
        return;
    }
    SetNextWindowSize(ImVec2(520, 480), ImGuiCond_Appearing);
    if (Begin("Select Audio Clip", &m_openClipPicker,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        InputTextWithHint("##search", "Search clip GUID...", &m_clipSearch);
        SameLine();
        if (Button("Refresh"))
        {
            m_clipKeyCache = m_clipPickerTarget->GetOwner()->GetScene()->Sounds().ClipKeys();
        }
        Separator();
        BeginChild("##cliplist", ImVec2(0, -48), true);
        for (std::size_t i = 0; i < m_clipKeyCache.size(); ++i)
        {
            const auto& key = m_clipKeyCache[i];
            if (!m_clipSearch.empty() && key.find(m_clipSearch) == std::string::npos)
            {
                continue;
            }
            if (Selectable(key.c_str(), false))
            {
                auto settings = m_clipPickerTarget->ReadSettings();
                settings.clipKey = key;
                settings.sourceKind = wave::SoundSourceKind::Clip;
                m_clipPickerTarget->QueueSettings(std::move(settings));
                m_openClipPicker = false;
            }
            SameLine();
            if (SmallButton((EditorIcon::Label<EditorIcon::Play, "##prev"> + std::to_string(i)).c_str()))
            {
                m_clipPickerTarget->QueuePreview(SoundComponent::PreviewCommand::OneShot, key);
            }
        }
        EndChild();
        if (Button("Close"))
        {
            m_openClipPicker = false;
        }
    }
    End();
}

// PHASE 21 W3: 생성자 안 람다였던 본문. 옮긴 것은 들여쓰기뿐이다.
void InspectorWindow::Draw()
{
	const editor::InspectorStyleScope inspectorStyle;


	Scene* scene = nullptr;
	RenderScene* renderScene = nullptr;
	Entity* selectedSceneObject = nullptr;
	std::optional<Authoring::WriteDocument>& selectedNode{
		ContentsBrowserWindow::selectedFileMetaNode };
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

	const EntityHandle current = selectedSceneObject ? scene->HandleOf(selectedSceneObject->m_index) : EntityHandle{};
    bool sceneObjectJustSelected = current.IsValid() && current != m_previousEntity;

	bool metaNodeJustSelected = (isSelectedNode && !m_wasMetaSelected);

	// 3. 우선순위 결정
	if (sceneObjectJustSelected)
	{
		// 게임 오브젝트 선택 시 YAML 선택 해제
		selectedNode = std::nullopt;
		isSelectedNode = false;
		m_wasMetaSelected = false;
	}

	if (metaNodeJustSelected)
	{
		// 메타 파일 선택 시 게임 오브젝트 해제
		selectedSceneObject = nullptr;
		m_previousEntity = {};
		m_wasMetaSelected = true;
	}

    selectedSceneObject = DrawNavigation(scene, selectedSceneObject);
    isSelectedNode = selectedNode.has_value();
    const EntityHandle inspected = selectedSceneObject ? scene->HandleOf(selectedSceneObject->m_index) : EntityHandle{};
    const bool changedTarget = inspected != m_previousEntity;
    const bool editLocked = EditorObjectOperations::IsEditLocked(selectedSceneObject, true);
    if (changedTarget || editLocked)
    {
        m_openClipPicker = false;
        m_clipPickerTarget = nullptr;
        m_openNewTagPopup = m_openNewLayerPopup = false;
    }
	TerrainBrush* terrainBrush = EditorSessionState::Get().FindTerrainBrush();
	if ((!selectedSceneObject || editLocked) && terrainBrush)
	{
		terrainBrush->m_isEditMode = false;
	}

    ImGui::BeginDisabled(editLocked);
	if (scene && selectedSceneObject)
	{
		g_inspectorEntity = selectedSceneObject->m_name.ToString();
		{
			inspector_body_probe probe;
			ImGuiDrawHelperGameObjectBaseInfo(selectedSceneObject);
			probe.finish("GameObjectBaseInfo", 0, true);
		}

		static bool isOpen = false;
		static Component* selectedComponent = nullptr;
        if (changedTarget || editLocked) { isOpen = false; selectedComponent = nullptr; }

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
		//
		// ★ 모든 컴포넌트가 이 순회 하나를 지난다 (W2-I1 · Transform 2안)
		//
		// 예전에는 공간 컴포넌트 둘을 순회 **앞에서** 전용 드로어로 따로 부르고 순회에서는
		// 건너뛰었다(1안). 머리줄·체크박스·메뉴를 드로어가 제각각 소유해, 건너뛰기 목록 한 줄이
		// 빠지면 Transform 이 두 번 나왔고(실제로 그랬다) 캔버스는 한쪽이 리플렉션으로 떨어졌다.
		// 이제 머리줄과 정책은 여기 한 자리가 소유하고 전용 드로어는 본문만 그린다. 표시 순서는
		// 정책의 `order` 로 정해 공간 컴포넌트가 위에 서고, 같은 순서 안에서는 붙은 순서를 지킨다.
		// 순서마다 한 번씩 훑는 것은 프레임마다 정렬 사본을 만들지 않기 위해서다.
		for (int orderPass = 0; orderPass < EditorObjectOperations::kComponentOrderCount; ++orderPass)
		for (size_t componentIndex = 0; componentIndex < selectedSceneObject->m_components.size(); ++componentIndex)
		{
			auto& component = selectedSceneObject->m_components[componentIndex];
			if (nullptr == component || component->IsDestroyMark())
				continue;
			const EditorObjectOperations::ComponentEditPolicy policy = EditorObjectOperations::PolicyOf(*component);
			if (policy.order != orderPass)
				continue;

			// CT1: 종전 Meta::Find(component->ToString())는 매 프레임 컴포넌트마다
			// 문자열 생성 + 문자열 해시 조회였다 — m_name이 타입명과 일치한다는
			// GENERATED_BODY 관행에 기댄 우회이기도 했다. typeID 조회는 항등이다
			// (Registry가 등록 시 이름 맵·해시 맵에 같은 Type을 넣는다).
			const auto& type = Meta::Find(component->GetTypeID());

			std::string componentBaseName = component->ToString();
			if (!type) continue;
			if (auto* script = dynamic_cast<ScriptComponent*>(component.get()))
				componentBaseName = script->m_scriptType.empty() ? "Missing (Script)"
					: editor::components::DisplayName(script->m_scriptType, true) + " (Script)";
			// Repeated script classes have independent foldouts, fields and context menus.
			ImGui::PushID(static_cast<int>(component->GetInstanceID()));

			// 체크박스에 m_isEnabled를 직접 물리면 SetEnabled를 건너뛰어
			// OnEnable/OnDisable이 영영 호출되지 않는다. 지역 값으로 받아
			// 전이가 생긴 프레임에만 컴포넌트에 알린다.
			bool isEnabled = component->IsEnabled();
			editor::widgets::inspector_panel_request componentPanel{};
			componentPanel.label = componentBaseName.c_str();
			componentPanel.icon = editor::inspector::inspector_icon(
				component->GetTypeID().m_ID_Data);
			componentPanel.menu_icon = EditorIcon::More;
			// 개별로 켜고 끌 수 없는 컴포넌트는 체크박스를 넘기지 않는다. 패널이 그 칸을 비워 두므로
			// 이름은 다른 컴포넌트와 같은 x 에 선다. 체크박스를 숨기는 것만으로 정책이 서지는 않는다 —
			// `object.property` 쪽도 같은 표로 거부한다.
			const bool characterAuthoring = dynamic_cast<CharacterMovementComponent*>(component.get()) != nullptr;
            componentPanel.enabled = policy.individuallyToggleable &&
                !(characterAuthoring && (SceneManagers->IsGameStart() ||
                    EditorObjectOperations::IsEditLocked(selectedSceneObject, true))) ? &isEnabled : nullptr;
			const editor::widgets::inspector_panel_result componentHeaderState =
				editor::widgets::begin_inspector_panel(componentPanel);
			// isOpen은 프레임을 건너 사는 정적 변수라, 아래에서 ComponentMenu를
			// 열고 스스로 끌 때까지 살아 있어야 한다. 원본도 눌린 프레임에만
			// true를 써 넣었다 — 안 눌렸다고 false로 덮으면 팝업이 뜨기 전에
			// 꺼진다.
			if (componentHeaderState.menu_clicked)
			{
				isOpen = true;
				selectedComponent = component.get();
			}
			const bool isHeaderOpen = componentHeaderState.open;
			inspector_body_probe bodyProbe;
			if (componentHeaderState.enabled_changed)
			{
                if (characterAuthoring)
                {
                    const auto target = selectedSceneObject->GetScene()->HandleOf(selectedSceneObject->m_index);
                    EditorObjectOperations::Property(target, "#" + std::to_string(component->GetInstanceID()),
                                                     "m_isEnabled", isEnabled ? "true" : "false");
                }
                else
                    component->SetEnabled(isEnabled);
			}

			if (isHeaderOpen)
			{
				if(isOpen && nullptr == selectedComponent)
				{
					selectedComponent = component.get();
				}
				auto componentTypeID = component->GetTypeID();
				if (componentTypeID == type_guid(Transform))
				{
					ImGuiDrawHelperTransformComponent(selectedSceneObject);
				}
				else if (componentTypeID == type_guid(RectTransformComponent))
				{
					if (auto* rectTransform = dynamic_cast<RectTransformComponent*>(component.get()))
						ImGuiDrawHelperRectTransformComponent(rectTransform);
				}
				else if(componentTypeID == type_guid(MeshRenderer))
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
				else if (componentTypeID == type_guid(SceneRenderProfileComponent))
				{
					SceneRenderProfileComponent* input = dynamic_cast<SceneRenderProfileComponent*>(component.get());
					if (nullptr != input)
					{
						ImGuiDrawHelperRenderProfile(input);
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
                else if (componentTypeID == type_guid(AudioListenerComponent))
                {
                    auto* listener = dynamic_cast<AudioListenerComponent*>(component.get());
                    if (listener)
                    {
                        auto settings = listener->ReadSettings();
                        bool changed = ImGui::Checkbox("Active listener", &settings.active);
                        changed |= ImGui::DragFloat3("Velocity", &settings.velocity.x, 0.01f);
                        ImGui::TextWrapped("Use one enabled active listener per world. Legacy scenes fall back to their primary camera.");
                        if (changed)
                        {
                            listener->QueueSettings(settings);
                        }
                    }
                }
				else if (componentTypeID == type_guid(SoundComponent))
				{
					SoundComponent* snd = dynamic_cast<SoundComponent*>(component.get());
					if (snd) ImGuiDrawHelperSoundComponent(snd);   // 커스텀 인스펙터 호출
				}
                else if (auto* character = dynamic_cast<CharacterMovementComponent*>(component.get()))
                {
                    draw_character_movement(*character, m_layout);
                }
                else if (auto* body = dynamic_cast<PhysicsBodyComponent*>(component.get()))
                {
                    ImGui::BeginDisabled(SceneManagers->IsGameStart());
                    Meta::DrawObject(component.get(), *type);
                    ImGui::EndDisabled();
                    draw_physics_shapes(*body);
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

			bodyProbe.finish(component->ToString(),
				static_cast<std::uint32_t>(component->GetInstanceID()), isHeaderOpen,
				nullptr != componentPanel.enabled);

			// 접혀 있어도 부른다. 여는 쪽이 ID 와 들여쓰기를 밀어 두기 때문에
			// 건너뛰면 그 뒤의 모든 줄이 한 칸씩 밀린 채 프레임이 끝난다.
			editor::widgets::end_inspector_panel();
			ImGui::PopID();
		}

		// W2-I3 자극물. 컴포넌트와 같은 순회·같은 측정을 지나야 판정이 같은 자로 읽힌다.
		if (g_inspectorFixture.load(std::memory_order_relaxed))
		{
			static editor::inspector::InspectorLayoutFixture fixture{};
			ImGui::PushID("ReflectionFixture");
			const bool fixtureOpen = editor::widgets::property_group_header("Reflection Fixture");
			inspector_body_probe probe;
			if (fixtureOpen) Meta::TypedDraw::DrawTypedObject(fixture);
			probe.finish("ReflectionFixture", 0, fixtureOpen);
			ImGui::PopID();
		}

		ImGui::Separator();
		DrawAddComponent(selectedSceneObject);

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



		if (ImGui::BeginPopup("ComponentMenu"))
		{
			// 제거할 수 없는 컴포넌트에는 항목을 내지 않는다. 눌러도 작업이 거부하지만, 거부될 항목을
			// 보여 주는 것은 정책을 화면에서 숨기는 일이다.
			if (selectedComponent && EditorObjectOperations::PolicyOf(*selectedComponent).removable &&
				ImGui::MenuItem("		Remove Component"))
			{
				EditorObjectOperations::RemoveComponent(selectedSceneObject->GetScene()->HandleOf(selectedSceneObject->m_index), "#" + std::to_string(selectedComponent->GetInstanceID()));
				ImGui::CloseCurrentPopup();
				selectedComponent = nullptr;
			}
			if (selectedComponent && selectedComponent->GetTypeID() == type_guid(Transform) &&
				ImGui::MenuItem("Reset Transform"))
			{
				ResetTransform(selectedSceneObject);
				ImGui::CloseCurrentPopup();
				selectedComponent = nullptr;
			}

			// 선언된 컴포넌트 팝업 항목(PHASE 21 M1). 문맥은 (엔티티 신원, 컴포넌트
			// 선택자) 둘이다. 선택자는 위 Remove Component 가 쓰는 것과 **같은** 형태인
			// "#<instanceID>" 다 — CLI 가 컴포넌트를 가리킬 때 쓰는 그 표기여서, 복사한
			// 값을 그대로 명령에 붙일 수 있다. 항목 0 이면 구분선조차 넣지 않는다(A.6).
			if (::editor::popup_host_has_items(::editor::popup_host::inspector_component) &&
				nullptr != selectedComponent && nullptr != selectedSceneObject)
			{
				ImGui::Separator();
				::editor::draw_popup_menu_items<::editor::popup_host::inspector_component>(
					::editor::component_target{
						EditorObjectOperations::ObjectId(
							selectedSceneObject->GetScene()->HandleOf(selectedSceneObject->m_index)),
						"#" + std::to_string(selectedComponent->GetInstanceID()) });
			}
			ImGui::EndPopup();
		}
	}
	else if (isSelectedNode)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(editor::ThemePixels(editor::EditorThemeTokens::PropertyGapX), editor::ThemePixels(editor::EditorThemeTokens::ItemGapY)));
		// 자산 이름은 왼쪽 정렬한다. 정렬 비율은 pixel 배율 대상이 아니다.
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

		std::string stem = selectedFileName.stem().string();

		stem += " Import Settings";

		const bool importOpen = ImGui::CollapsingHeader(stem.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		{
			inspector_body_probe probe;
			if (importOpen) DrawYamlNodeEditor(selectedNode->Root());
			probe.finish("ImportSettings", 0, importOpen);
		}
		if (importOpen)
		{

			ImGui::Spacing();
			if (ImGui::Button("Save"))
			{
				try
				{
					std::ofstream fout(selectedMetaFilePath, std::ios::binary | std::ios::trunc);
					if (fout.is_open())
					{
						fout << selectedNode->Dump();
						fout.close();
					}
					else
					{
						Debug::PrintLog(spdlog::level::err, "Failed to open file for writing: " + selectedMetaFilePath.string());
					}
				}
				catch (const std::exception& e)
				{
					Debug::PrintLog(spdlog::level::err, "Failed to save YAML: " + std::string(e.what()));
				}
			}
		}
		ImGui::PopStyleVar(2);
	}


    ImGui::EndDisabled();
	// 디버그 모드 토글 (PHASE 21 W2-I).
	//
	// `[[creator::debug_only]]` 로 표시한 항목은 이 모드에서만 그려진다. 내부
	// 식별자처럼 평소엔 잡음이지만 문제를 쫓을 때는 봐야 하는 것들이다.
	//
	// 빈 자리 오른쪽 클릭으로 연다 — 줄을 하나도 쓰지 않는다. 항목 위에서는
	// 열리지 않게 막아 컴포넌트의 제 문맥 메뉴와 겹치지 않는다.
	//
	// ★ 창의 **끝**에서 부른다. 앞에서 부르면 `NoOpenOverItems` 가 보는
	//    `IsAnyItemHovered` 가 이번 프레임의 항목을 아직 하나도 못 본 상태라
	//    직전 프레임의 값으로 판정한다 — 스크롤바를 만졌다가 빈 자리를
	//    눌렀더니 메뉴가 안 열렸다. 항목을 다 낸 뒤에 물어야 맞는 답이 온다.
	if (ImGui::BeginPopupContextWindow("##InspectorOptions",
		ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		bool debugMode = editor::widgets::property_debug_mode();
		if (ImGui::MenuItem("Debug Mode", nullptr, &debugMode))
		{
			editor::widgets::set_property_debug_mode(debugMode);
		}
		ImGui::EndPopup();
	}

	m_previousEntity = inspected;
	m_wasMetaSelected = isSelectedNode;
}
