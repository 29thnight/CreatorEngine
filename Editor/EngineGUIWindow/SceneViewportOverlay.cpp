#include "SceneViewportOverlay.h"
#include "EditorCameraRig.h"
#include "EditorTheme.h"
#include "EditorIcons.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "GizmoRenderer.h"
#include "EnhancedRenderDebugWindow.h"
#include "../../ThirdParty/ImViewGuizmo/ImViewGuizmo.h"
#include <cstdio>
#include <mutex>
#include <string>

namespace editor::scene_overlay_detail
{
    std::mutex snapshotMutex;
    SceneOverlaySnapshot snapshot;
    constexpr const char* tools[]{EditorIcon::Select, EditorIcon::Move, EditorIcon::Rotate, EditorIcon::Scale};
    constexpr const char* toolNames[]{"Select (Q)", "Move (W)", "Rotate (E)", "Scale (R)"};
    constexpr const char* snapNames[]{"Translation snap", "Rotation snap", "Scale snap"};
    constexpr const char* snapIcons[]{EditorIcon::Snap, EditorIcon::Rotate, EditorIcon::Scale};

    struct ToolbarStyle
    {
        ToolbarStyle(float scale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.f * scale, 3.f * scale});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.19f, 0.21f, 0.94f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.29f, 0.33f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeColorValue(ThemeColor::Primary));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.07f, 0.09f, 0.11f, 1));
        }
        ~ToolbarStyle() { ImGui::PopStyleColor(4); ImGui::PopStyleVar(4); }
    };

    bool Button(const char* id, const char* label, const char* tip, float width, float height,
        bool selected = false, ImDrawFlags corners = ImDrawFlags_RoundCornersAll)
    {
        ImGui::PushID(id);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##button", {width, height}, ImGuiButtonFlags_EnableNav);
        auto color = selected ? ThemeColorValue(ThemeColor::Primary)
            : ImGui::GetStyleColorVec4(ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p, {p.x + width, p.y + height}, ImGui::GetColorU32(color), height * 0.5f, corners);
        draw->AddRect(p, {p.x + width, p.y + height}, ImGui::GetColorU32(ImGuiCol_Border), height * 0.5f, 1.f, corners);
        if (ImGui::IsItemFocused()) draw->AddRect(p, {p.x + width, p.y + height},
            ImGui::GetColorU32(ImGuiCol_NavCursor), height * 0.5f, 2.f, corners);
        const ImVec2 text = ImGui::CalcTextSize(label);
        draw->PushClipRect(p, {p.x + width, p.y + height}, true);
        draw->AddText({p.x + (width - text.x) * 0.5f, p.y + (height - text.y) * 0.5f},
            ImGui::GetColorU32(ImGuiCol_Text), label);
        draw->PopClipRect();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tip);
        ImGui::PopID();
        return pressed;
    }
}

editor::SceneOverlaySnapshot editor::ReadSceneOverlaySnapshot()
{
    std::lock_guard lock(scene_overlay_detail::snapshotMutex);
    return scene_overlay_detail::snapshot;
}

void editor::SceneViewportOverlay::Draw(ImVec2 imageMin, ImVec2 imageMax,
    EditorCameraRig& rig, GizmoRenderer* gizmos, const ViewportCanvas& canvas)
{
    using namespace scene_overlay_detail;
    Camera& cam = rig.GetCamera();
    const float scale = ThemePixels(1.f), h = ThemePixels(24.f), gap = ThemePixels(6.f);
    const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
    const auto labelWidth = [scale](const char* text) { return ImGui::CalcTextSize(text).x + 16.f * scale; };
    const char* projection = cam.m_isOrthographic ? EditorIcon::Label<EditorIcon::Orthographic, " Orthographic">
        : EditorIcon::Label<EditorIcon::Perspective, " Perspective">;
    const char* shading = gizmos && gizmos->IsWireFrameEnabled()
        ? EditorIcon::Label<EditorIcon::Lit, " Lit + Wire"> : EditorIcon::Label<EditorIcon::Lit, " Lit">;
    std::array<std::string, 3> snapLabels;
    std::array<float, 3> snapWidths;
    for (int i = 0; i < 3; ++i)
    {
        char value[48]{};
        std::snprintf(value, sizeof(value), i == 1 ? "%g\xc2\xb0" : "%g", snapValues[i]);
        snapLabels[i] = value;
        snapWidths[i] = (std::max)(h, labelWidth(value));
    }
    const float fullLeft = h + labelWidth(projection) + labelWidth(shading) + labelWidth("Show") + 3 * gap;
    const float fullRight = 4 * h + h + 5 * gap + 3 * h + snapWidths[0] + snapWidths[1] + snapWidths[2] + h;
    const float compactRight = 4 * h + h + 3 * gap + h + snapWidths[(std::max)(0, operation - 1)] + h;
    const auto layout = LayoutSceneToolbar(imageMin, imageMax, scale, fullLeft, fullRight, compactRight);
    bool popupWasOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    bool pointerOverToolbar = ImGui::IsMouseHoveringRect(layout.left,
        {layout.left.x + layout.leftWidth, layout.left.y + h}) ||
        (layout.rightWidth > 0 && ImGui::IsMouseHoveringRect(layout.right,
            {layout.right.x + layout.rightWidth, layout.right.y + h}));
    bool openStatistics = false, openCamera = false;
    const auto projectionMenu = [&] {
        if (ImGui::MenuItem("Perspective", nullptr, !cam.m_isOrthographic)) cam.m_isOrthographic = false;
        if (ImGui::MenuItem("Orthographic", nullptr, cam.m_isOrthographic)) cam.m_isOrthographic = true;
    };
    const auto shadingMenu = [&] {
        if (ImGui::MenuItem("Lit", nullptr, gizmos && !gizmos->IsWireFrameEnabled(), gizmos != nullptr)
            && gizmos->IsWireFrameEnabled()) gizmos->SetWireFrame();
        if (ImGui::MenuItem("Wireframe overlay", nullptr, gizmos && gizmos->IsWireFrameEnabled(), gizmos != nullptr))
            gizmos->SetWireFrame();
        ImGui::Separator();
        if (ImGui::MenuItem("Render Pass settings")) open_window(EditorWindowName::kRenderPass);
    };
    const auto showMenu = [&] {
        ImGui::MenuItem("Orientation gizmo", nullptr, &showViewGizmo);
        if (ImGui::MenuItem("Grid settings")) open_window(EditorWindowName::kGridSettings);
        if (ImGui::MenuItem("Render Statistics")) openStatistics = true;
    };
    const auto snapMenu = [&](int i) {
        ImGui::Checkbox(snapNames[i], &snapEnabled[i]);
        const float translation[]{0.1f, 0.5f, 1.f, 5.f, 10.f, 50.f, 100.f};
        const float rotation[]{5.f, 10.f, 15.f, 30.f, 45.f, 90.f};
        const float scaling[]{0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.f};
        const float* values = i == 0 ? translation : i == 1 ? rotation : scaling;
        const int count = i == 0 ? 7 : 6;
        for (int j = 0; j < count; ++j)
        {
            char value[32]{}; std::snprintf(value, sizeof(value), "%g", values[j]);
            if (ImGui::MenuItem(value, nullptr, snapValues[i] == values[j])) snapValues[i] = values[j];
        }
        ImGui::SetNextItemWidth(ThemePixels(140.f));
        ImGui::DragFloat("Custom", &snapValues[i], 0.01f, 0.001f, 1000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    };
    const auto allControls = [&] {
        if (ImGui::BeginMenu("Projection")) { projectionMenu(); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Shading")) { shadingMenu(); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Show")) { showMenu(); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Transform"))
        {
            for (int i = 0; i < 4; ++i) if (ImGui::MenuItem(toolNames[i], nullptr, operation == i)) operation = i;
            ImGui::Separator();
            ImGui::MenuItem("Local coordinates", nullptr, &local);
            ImGui::EndMenu();
        }
        for (int i = 0; i < 3; ++i)
        { ImGui::PushID(i); if (ImGui::BeginMenu(snapNames[i])) { snapMenu(i); ImGui::EndMenu(); } ImGui::PopID(); }
        if (ImGui::MenuItem("Camera settings")) openCamera = true;
        if (ImGui::MenuItem("Render Statistics")) openStatistics = true;
    };
    {
        const ToolbarStyle style(scale);
        ImGui::SetCursorScreenPos(layout.left);
        if (Button("viewport.menu", EditorIcon::Menu, "Viewport options", layout.leftWidth > h ? h : layout.leftWidth, h))
            ImGui::OpenPopup("SceneOptions");
        if (layout.mode == SceneToolbarMode::Full)
        {
            ImGui::SameLine(0, gap);
            if (Button("viewport.projection", projection, "Projection", labelWidth(projection), h)) ImGui::OpenPopup("SceneProjection");
            ImGui::SameLine(0, gap);
            if (Button("viewport.shading", shading, "Shading", labelWidth(shading), h)) ImGui::OpenPopup("SceneShading");
            ImGui::SameLine(0, gap);
            if (Button("viewport.show", "Show", "Visibility and statistics", labelWidth("Show"), h)) ImGui::OpenPopup("SceneShow");
        }
        if (layout.rightWidth > 0)
        {
            ImGui::SetCursorScreenPos(layout.right);
            for (int i = 0; i < 4; ++i)
            {
                ImGui::PushID(i);
                if (i) ImGui::SameLine(0, 0);
                if (Button("viewport.tool", tools[i], toolNames[i], h, h, operation == i,
                    i == 0 ? ImDrawFlags_RoundCornersLeft : i == 3 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone)) operation = i;
                ImGui::PopID();
            }
            if (layout.mode == SceneToolbarMode::Full || layout.mode == SceneToolbarMode::Compact)
            {
                ImGui::SameLine(0, gap);
                if (Button("viewport.space", local ? EditorIcon::GameObject : EditorIcon::World,
                    local ? "Local coordinates" : "World coordinates", h, h)) local = !local;
                for (int i = 0; i < 3; ++i)
                {
                    if (layout.mode == SceneToolbarMode::Compact && i != (std::max)(0, operation - 1)) continue;
                    ImGui::PushID(i);
                    ImGui::SameLine(0, gap);
                    if (Button("viewport.snap", snapIcons[i], snapNames[i], h, h, snapEnabled[i], ImDrawFlags_RoundCornersLeft)) snapEnabled[i] = !snapEnabled[i];
                    ImGui::SameLine(0, 0);
                    if (Button("viewport.snap.value", snapLabels[i].c_str(), snapNames[i], snapWidths[i], h, false, ImDrawFlags_RoundCornersRight)) ImGui::OpenPopup("SnapOptions");
                    if (ImGui::BeginPopup("SnapOptions")) { snapMenu(i); ImGui::EndPopup(); }
                    ImGui::PopID();
                }
                ImGui::SameLine(0, gap);
                if (Button("viewport.camera", layout.mode == SceneToolbarMode::Full ? EditorIcon::Camera : EditorIcon::More,
                    layout.mode == SceneToolbarMode::Full ? "Camera settings" : "More viewport controls", h, h))
                { if (layout.mode == SceneToolbarMode::Full) openCamera = true; else ImGui::OpenPopup("SceneMore"); }
            }
            else
            {
                ImGui::SameLine(0, 0);
                if (Button("viewport.more", EditorIcon::More, "More viewport controls", h, h)) ImGui::OpenPopup("SceneMore");
            }
        }
    }
    // Restore regular popup spacing; toolbar pills do not leak into menus.
    if (ImGui::BeginPopup("SceneOptions")) { allControls(); ImGui::EndPopup(); }
    if (ImGui::BeginPopup("SceneMore")) { allControls(); ImGui::EndPopup(); }
    if (ImGui::BeginPopup("SceneProjection")) { projectionMenu(); ImGui::EndPopup(); }
    if (ImGui::BeginPopup("SceneShading")) { shadingMenu(); ImGui::EndPopup(); }
    if (ImGui::BeginPopup("SceneShow")) { showMenu(); ImGui::EndPopup(); }
    if (openCamera) ImGui::OpenPopup("CameraSettings");
    if (openStatistics) ImGui::OpenPopup("RenderStatistics");
    ImGui::SetNextWindowSizeConstraints({ThemePixels(240.f), 0}, {ThemePixels(440.f), (std::max)(h, imageMax.y - imageMin.y)});
    if (ImGui::BeginPopup("CameraSettings"))
    {
        ImGui::SetNextItemWidth(ThemePixels(140));
        ImGui::SliderFloat("FOV", &cam.m_fov, 1.f, 179.f);
        ImGui::DragFloat("Near Plane", &cam.m_nearPlane, 0.01f, 0.001f, cam.m_farPlane - 0.001f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Far Plane", &cam.m_farPlane, 1.f, cam.m_nearPlane + 0.001f, 100000.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Width", &cam.m_viewWidth, 0.1f, 0.01f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Height", &cam.m_viewHeight, 0.1f, 0.01f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Camera Speed", rig.SpeedPtr(), 0.1f, 0.f, 200.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSizeConstraints({ThemePixels(280.f), 0}, {ThemePixels(520.f), (std::max)(h, imageMax.y - imageMin.y)});
    if (ImGui::BeginPopup("RenderStatistics")) { DrawSceneRenderStatistics(); ImGui::EndPopup(); }
    const bool anyPopup = popupWasOpen || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool windowHovered = ImGui::IsWindowHovered();
    const bool drawViewGizmo = showViewGizmo && layout.showGizmo;
    bool viewUsing = false;
    ImViewGuizmo::BeginFrame();
    if (drawViewGizmo)
    {
        auto& style = ImViewGuizmo::GetStyle();
        style.scale = scale * 0.5f; // disc radius 80 * 0.5 logical pixels
        style.lineLength = 0.48f;
        style.circleRadius = 16.f;
        style.lineWidth = 2.5f;
        style.labelSize = 0.75f;
        style.bigCircleColor = IM_COL32(135, 135, 135, 85);
        style.highlightColor = IM_COL32(235, 235, 235, 255);
        style.snapAnimationDuration = 0.25f;
        auto& context = ImViewGuizmo::GetContext();
        const bool canUse = windowHovered && !anyPopup && !pointerOverToolbar && !ImGui::IsAnyItemActive();
        // The upstream draw-list widget has no ImGui item to perform popup/focus hit tests.
        if (!canUse && !ImViewGuizmo::IsUsing()) context.hoveredAxisID = -1;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        { context.activeTool = ImViewGuizmo::TOOL_NONE; context.isAnimating = false; }
        if (!ImViewGuizmo::IsUsing()) orbitPivot = cam.m_eyePosition + cam.m_forward * 8.f;
        ImGuiIO& io = ImGui::GetIO();
        const ImGuiConfigFlags flags = io.ConfigFlags;
        if (!canUse && !ImViewGuizmo::IsUsing()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        if (ImViewGuizmo::Rotate(cam.m_eyePosition, cam.rotate, orbitPivot, layout.gizmoCenter))
            rig.SetPose(cam.m_eyePosition, cam.rotate);
        io.ConfigFlags = flags;
        viewUsing = ImViewGuizmo::IsUsing();
        pointerOverToolbar |= canUse && ImViewGuizmo::IsOver();
    }
    else
    {
        auto& context = ImViewGuizmo::GetContext();
        context.activeTool = ImViewGuizmo::TOOL_NONE; context.isAnimating = false;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) leftOwned = pointerOverToolbar || anyPopup || viewUsing;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rightOwned = pointerOverToolbar || anyPopup;
    blocksPointer = pointerOverToolbar || anyPopup || viewUsing || leftOwned || rightOwned;
    blocksShortcuts = anyPopup || ImGui::GetIO().WantTextInput || viewUsing;
    if (!blocksShortcuts && ImGui::IsWindowFocused() && !ImGui::GetIO().KeyCtrl && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) operation = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) operation = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) operation = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) operation = 3;
        if (operation > 0 && ImGui::IsKeyPressed(ImGuiKey_T)) snapEnabled[operation - 1] = !snapEnabled[operation - 1];
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) leftOwned = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) rightOwned = false;
    ImGui::SetCursorScreenPos(savedCursor);
    ImGui::Dummy({0.f, 0.f}); // Complete the restored cursor's layout item (ImGui 1.92).
    std::lock_guard lock(snapshotMutex);
    snapshot = {true, blocksPointer, drawViewGizmo, viewUsing, cam.m_isOrthographic, local, layout.mode,
        imageMin, imageMax, layout.left, layout.right, layout.gizmoCenter,
        layout.leftWidth, layout.rightWidth, h, layout.radius, operation, cam.m_eyePosition, cam.m_forward, canvas};
}
