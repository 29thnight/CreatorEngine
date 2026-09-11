#pragma once

#include "IconsFontAwesome6.h"

// 에디터 창 이름의 정본.
//
// ── 왜 상수로 모으는가 ──
//
// ImGui는 창을 이름으로 식별한다. 도크 빌더·Window 메뉴·창 본문이 각자
// 리터럴을 적으면 한 글자만 달라도 조용히 다른 창이 된다. 실제로 그랬다:
// 초기 도크 레이아웃은 `ICON_FA_HARD_DRIVE "  Content Browser"`(공백 둘)를
// 도크했고 실제 창은 `ICON_FA_HARD_DRIVE " Content Browser"`(공백 하나)였다.
// 그래서 imgui.ini가 없는 최초 실행에서 Content Browser는 어느 노드에도
// 붙지 않고 떠 있었다 — 오타 하나가 유령 노드를 만든 것이다 (2026-09-10 실측).
//
// 이 헤더는 그 식별자를 한 곳으로 모은다. 이름 뒤에 붙은 공백은 탭 폭을
// 벌리려고 넣은 것이라 지금 지우면 기존 imgui.ini의 도크 항목이 전부
// 어긋난다 — 표시 이름과 안정 식별자를 가르는 일은 W3의 몫이다.
namespace EditorWindowName
{
    inline constexpr const char* kScene = ICON_FA_USERS_VIEWFINDER "  Scene      ";
    inline constexpr const char* kGame = ICON_FA_GAMEPAD "  Game        ";
    inline constexpr const char* kHierarchy = ICON_FA_BARS_STAGGERED "  Hierarchy";
    inline constexpr const char* kInspector = ICON_FA_CIRCLE_INFO "  Inspector";
    inline constexpr const char* kAssetBundle = ICON_FA_DIAGRAM_PROJECT "  AssetBundle";
    inline constexpr const char* kContentBrowser = ICON_FA_HARD_DRIVE " Content Browser";

    // ── 표시 이름이 안정 식별자와 갈린 첫 창 ──────────────────────────────
    //
    // 위의 `kContentBrowser`는 **화면에 나오지 않는다.** `###` 오른쪽에만 실려
    // 기존 `imgui.ini`의 도크 항목을 붙잡는 값이고, 사람이 보는 것은 아래
    // 라벨이다. 셸이 `라벨###안정식별자`로 `Begin`을 부르고 `ImHashStr`이
    // `###` 앞을 버리므로, 라벨을 바꿔도 창 id가 한 비트도 달라지지 않는다.
    //
    // 하드디스크 글리프는 자산 브라우저의 뜻과 맞지 않았다. 참조로 삼은
    // S&Box Asset Browser의 탭이 폴더이고(계획서 §3), 왼쪽 트리와 자산 격자
    // 양쪽이 폴더 계열 글리프로 서 있다. `ICON_FA_FOLDER`를 고른 이유는 뜻만이
    // 아니라 **폰트 블롭에 실제로 있는 것이 확인된 글리프**여서다 — 없는
    // 글리프를 고르면 네모 한 칸이 그려지고 아무도 실패로 읽지 못한다.
    // 그 확인을 사람 눈에 맡기지 않으려고 `editor.theme`가 선언된 라벨 전체의
    // 글리프 누락을 센다(W0 후반).
    //
    // 나머지 창들의 라벨은 아직 안정 식별자와 같은 값이라 따로 두지 않는다.
    // W3이 안정 식별자를 `Editor.*`로 옮길 때 이 갈림이 전부에게 생긴다.
    inline constexpr const char* kContentBrowserLabel = ICON_FA_FOLDER " Content Browser";
    inline constexpr const char* kResourceCounter = "Resource Counter";
    inline constexpr const char* kRenderPass = "RenderPass";
    inline constexpr const char* kBehaviorTree = "Behavior Tree Editor";
    inline constexpr const char* kBlackBoard = "BlackBoard Editor";

    // 아래 넷은 도크되지 않는 도구 창이다. M4 2단계가 선언으로 옮기면서
    // 흩어져 있던 리터럴을 여기로 모았다 — 이름이 곧 안정 식별자라
    // 한 글자만 달라져도 배치와 여닫기가 조용히 다른 창을 가리킨다.
    inline constexpr const char* kLightMap = "LightMap";
    inline constexpr const char* kCollisionMatrix = "CollisionMatrixPopup";
    inline constexpr const char* kTextureImportSelector = "TextureType Selector";
    inline constexpr const char* kMaterialPicker = "SelectMaterial";

    // M4 3단계가 직접 `ImGui::Begin`을 부르던 열다섯을 옮기며 모은 이름들이다.
    // 값은 그때 `Begin`에 넘기던 리터럴 그대로다 — 한 글자라도 바꾸면 기존
    // imgui.ini의 도크·위치 항목이 어긋난다.
    inline constexpr const char* kFrameProfiler = ICON_FA_CHART_BAR " FrameProfiler";
    inline constexpr const char* kOutputLog = ICON_FA_TERMINAL " Log";
    inline constexpr const char* kAbout = "About Creator Engine";
    inline constexpr const char* kInputActionMaps = "InputActionMaps";
    inline constexpr const char* kBuildSceneSetting = "Build Scene Setting";
    inline constexpr const char* kRenderPassDebug = "RenderPass Debug";
    inline constexpr const char* kGridSettings = "Grid Settings";
    inline constexpr const char* kModelLoading = "Model loading";
    inline constexpr const char* kAnimatorEvent = "Event";
    inline constexpr const char* kAnimationControllers = "Animation Controllers";
    inline constexpr const char* kAvatarMask = "AvatarMask";
}
