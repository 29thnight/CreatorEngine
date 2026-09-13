#pragma once
#include "imgui.h"
#include "EditorViewportCanvas.h"
#include <algorithm>
#include <array>
#include <mathematics/vector3.hpp>

class EditorCameraRig;
class GizmoRenderer;

namespace editor
{
    enum class SceneToolbarMode { Full, Compact, Tools, Menu };
    struct SceneToolbarLayout
    {
        SceneToolbarMode mode{};
        ImVec2 left{}, right{}, gizmoCenter{};
        float height{}, radius{}, leftWidth{}, rightWidth{};
        bool showGizmo{};
    };

    inline SceneToolbarLayout LayoutSceneToolbar(ImVec2 min, ImVec2 max, float scale,
        float fullLeft, float fullRight, float compactRight)
    {
        SceneToolbarLayout r{};
        const float margin = 6.f * scale, gap = 6.f * scale, h = 24.f * scale;
        const float available = (std::max)(0.f, max.x - min.x - 2.f * margin);
        r.height = h;
        r.left = {min.x + margin, min.y + margin};
        r.leftWidth = h;
        if (fullLeft + fullRight + gap <= available)
        { r.mode = SceneToolbarMode::Full; r.leftWidth = fullLeft; r.rightWidth = fullRight; }
        else if (h + compactRight + gap <= available)
        { r.mode = SceneToolbarMode::Compact; r.rightWidth = compactRight; }
        else if (h + 5.f * h + gap <= available)
        { r.mode = SceneToolbarMode::Tools; r.rightWidth = 5.f * h; }
        else
        { r.mode = SceneToolbarMode::Menu; r.rightWidth = 0.f; r.leftWidth = (std::min)(h, available); }
        r.right = {max.x - margin - r.rightWidth, r.left.y};
        r.radius = 40.f * scale;
        r.gizmoCenter = {max.x - margin - r.radius, r.left.y + h + gap + r.radius};
        r.showGizmo = max.y >= r.gizmoCenter.y + r.radius + margin && available >= 2.f * r.radius;
        return r;
    }

    struct SceneViewportOverlay
    {
        int operation{1}; // Select, Move, Rotate, Scale
        bool local{true};
        bool snapEnabled[3]{false, false, false};
        float snapValues[3]{1.f, 15.f, 0.25f};
        bool showViewGizmo{true};
        bool leftOwned{}, rightOwned{};
        bool blocksPointer{}, blocksShortcuts{};
        math::vector3 orbitPivot{};

        void Draw(ImVec2 imageMin, ImVec2 imageMax, EditorCameraRig& camera, GizmoRenderer* gizmos,
            const ViewportCanvas& canvas);
        float* ActiveSnap() { return operation > 0 && snapEnabled[operation - 1] ? &snapValues[operation - 1] : nullptr; }
    };

    // A value snapshot published from the UI thread; command callers never touch ImGui.
    struct SceneOverlaySnapshot
    {
        bool valid{}, blocked{}, gizmoVisible{}, gizmoUsing{}, orthographic{}, local{};
        SceneToolbarMode mode{};
        ImVec2 imageMin{}, imageMax{}, left{}, right{}, gizmoCenter{};
        float leftWidth{}, rightWidth{}, toolbarHeight{}, gizmoRadius{};
        int operation{};
        math::vector3 cameraPosition{}, cameraForward{};
        ViewportCanvas canvas;
    };
    SceneOverlaySnapshot ReadSceneOverlaySnapshot();
}
