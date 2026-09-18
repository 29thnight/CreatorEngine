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

        // 사각형을 따로 받지 않는다 — 자리는 캔버스가 정본이다(§1.5).
        // 예전에는 `imageMin`/`imageMax` 를 인자로 받으면서 캔버스도 함께 받아
        // 출처가 둘이었고, 배치는 인자 쪽으로 했다. 값이 같아 보이는 동안에는
        // 드러나지 않지만 정책이 갈리는 순간(crop 은 image 가 content 를 넘는다)
        // 툴바가 화면 밖으로 나간다.
        void Draw(EditorCameraRig& camera, GizmoRenderer* gizmos,
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
