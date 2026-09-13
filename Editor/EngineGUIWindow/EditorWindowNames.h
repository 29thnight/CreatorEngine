#pragma once

#include "EditorIcons.h"
#include <string_view>

// Workspace v1 IDs are independent of displayed labels, fonts and localization.
// The exact previous bytes remain in legacy_names for lossless layout migration.
namespace EditorWindowName
{
    // W4: 가운데는 창 하나다. Scene/Game 은 그 창의 표시 모드이고, 두 이름은
    // 모드 토큰과 legacy 배치 이주의 출발점으로만 남는다.
    inline constexpr const char* kViewport = "###Editor.Viewport";
    inline constexpr const char* kGamePreview = "###Editor.GamePreview";
    inline constexpr const char* kScene = "###Editor.Scene";
    inline constexpr const char* kGame = "###Editor.Game";
    inline constexpr const char* kHierarchy = "###Editor.Hierarchy";
    inline constexpr const char* kInspector = "###Editor.Inspector";
    inline constexpr const char* kAssetBundle = "###Editor.AssetBundle";
    inline constexpr const char* kContentBrowser = "###Editor.ContentBrowser";

    inline constexpr const char* kViewportLabel = EditorIcon::Label<EditorIcon::Scene, "  Viewport">;
    inline constexpr const char* kGamePreviewLabel = EditorIcon::Label<EditorIcon::Game, "  Game Preview">;
    inline constexpr const char* kSceneLabel = EditorIcon::Label<EditorIcon::Scene, "  Scene      ">;
    inline constexpr const char* kGameLabel = EditorIcon::Label<EditorIcon::Game, "  Game        ">;
    inline constexpr const char* kHierarchyLabel = EditorIcon::Label<EditorIcon::Hierarchy, "  Hierarchy">;
    inline constexpr const char* kInspectorLabel = EditorIcon::Label<EditorIcon::Inspector, "  Inspector">;
    inline constexpr const char* kAssetBundleLabel = EditorIcon::Label<EditorIcon::AssetBundle, "  AssetBundle">;
    inline constexpr const char* kContentBrowserLabel = EditorIcon::Label<EditorIcon::ContentBrowser, " Content Browser">;
    inline constexpr const char* kFrameProfilerLabel = EditorIcon::Label<EditorIcon::Profiler, " FrameProfiler">;
    inline constexpr const char* kOutputLogLabel = EditorIcon::Label<EditorIcon::Console, " Log">;

    inline constexpr const char* kResourceCounterLabel = "Resource Counter";
    inline constexpr const char* kResourceCounter = "###Editor.ResourceCounter";
    inline constexpr const char* kRenderPassLabel = "RenderPass";
    inline constexpr const char* kRenderPass = "###Editor.RenderPass";
    inline constexpr const char* kBehaviorTreeLabel = "Behavior Tree Editor";
    inline constexpr const char* kBehaviorTree = "###Editor.BehaviorTree";
    inline constexpr const char* kBlackBoardLabel = "BlackBoard Editor";
    inline constexpr const char* kBlackBoard = "###Editor.BlackBoard";

    // 아래 넷은 도크되지 않는 도구 창이다. M4 2단계가 선언으로 옮기면서
    // 흩어져 있던 리터럴을 여기로 모았다 — 이름이 곧 안정 식별자라
    // 한 글자만 달라져도 배치와 여닫기가 조용히 다른 창을 가리킨다.
    inline constexpr const char* kLightMapLabel = "LightMap";
    inline constexpr const char* kLightMap = "###Editor.LightMap";
    inline constexpr const char* kCollisionMatrixLabel = "CollisionMatrixPopup";
    inline constexpr const char* kCollisionMatrix = "###Editor.CollisionMatrix";
    inline constexpr const char* kTextureImportSelectorLabel = "TextureType Selector";
    inline constexpr const char* kTextureImportSelector = "###Editor.TextureImportSelector";
    inline constexpr const char* kMaterialPickerLabel = "SelectMaterial";
    inline constexpr const char* kMaterialPicker = "###Editor.MaterialPicker";

    // M4 3단계가 직접 `ImGui::Begin`을 부르던 열다섯을 옮기며 모은 이름들이다.
    // 값은 그때 `Begin`에 넘기던 리터럴 그대로다 — 한 글자라도 바꾸면 기존
    // imgui.ini의 도크·위치 항목이 어긋난다.
    inline constexpr const char* kFrameProfiler = "###Editor.FrameProfiler";
    inline constexpr const char* kOutputLog = "###Editor.OutputLog";
    inline constexpr const char* kAboutLabel = "About Creator Engine";
    inline constexpr const char* kAbout = "###Editor.About";
    inline constexpr const char* kInputActionMapsLabel = "InputActionMaps";
    inline constexpr const char* kInputActionMaps = "###Editor.InputActionMaps";
    inline constexpr const char* kBuildSceneSettingLabel = "Build Scene Setting";
    inline constexpr const char* kBuildSceneSetting = "###Editor.BuildSceneSetting";
    inline constexpr const char* kRenderPassDebugLabel = "RenderPass Debug";
    inline constexpr const char* kRenderPassDebug = "###Editor.RenderPassDebug";
    inline constexpr const char* kGridSettingsLabel = "Grid Settings";
    inline constexpr const char* kGridSettings = "###Editor.GridSettings";
    inline constexpr const char* kModelLoadingLabel = "Model loading";
    inline constexpr const char* kModelLoading = "###Editor.ModelLoading";
    inline constexpr const char* kAnimatorEventLabel = "Event";
    inline constexpr const char* kAnimatorEvent = "###Editor.AnimatorEvent";
    inline constexpr const char* kAnimationControllersLabel = "Animation Controllers";
    inline constexpr const char* kAnimationControllers = "###Editor.AnimationControllers";
    inline constexpr const char* kAvatarMaskLabel = "AvatarMask";
    inline constexpr const char* kAvatarMask = "###Editor.AvatarMask";
}

namespace EditorWindowName
{
    struct legacy_name { std::string_view old_name, stable_id; };
    inline constexpr legacy_name legacy_names[]{
        // W4: 옛 Scene/Game 두 창의 배치는 가운데 Host 하나로 접힌다. 둘 다
        // 같은 안정 ID 로 보내면 `migrate_ini` 의 승자 규칙이 도크된 쪽을 남긴다.
        { "\xee\x96\x95" "  Scene      ", kViewport },
        { "\xef\x84\x9b" "  Game        ", kViewport },
        { "###Editor.Scene", kViewport },
        { "###Editor.Game", kViewport },
        { "Editor.Scene", kViewport },
        { "Editor.Game", kViewport },
        { "\xef\x95\x90" "  Hierarchy", kHierarchy },
        { "\xef\x81\x9a" "  Inspector", kInspector },
        { "\xef\x95\x82" "  AssetBundle", kAssetBundle },
        { "\xef\x82\xa0" " Content Browser", kContentBrowser },
        { "Resource Counter", kResourceCounter },
        { "RenderPass", kRenderPass },
        { "Behavior Tree Editor", kBehaviorTree },
        { "BlackBoard Editor", kBlackBoard },
        { "LightMap", kLightMap },
        { "CollisionMatrixPopup", kCollisionMatrix },
        { "TextureType Selector", kTextureImportSelector },
        { "SelectMaterial", kMaterialPicker },
        { "\xef\x82\x80" " FrameProfiler", kFrameProfiler },
        { "\xef\x84\xa0" " Log", kOutputLog },
        { "About Creator Engine", kAbout },
        { "InputActionMaps", kInputActionMaps },
        { "Build Scene Setting", kBuildSceneSetting },
        { "RenderPass Debug", kRenderPassDebug },
        { "Grid Settings", kGridSettings },
        { "Model loading", kModelLoading },
        { "Event", kAnimatorEvent },
        { "Animation Controllers", kAnimationControllers },
        { "AvatarMask", kAvatarMask },
        { "\xef\x82\xa0" "  Content Browser", kContentBrowser },
    };
}
