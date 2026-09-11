#pragma once
// 에디터 메뉴 표면 정의 (PHASE 21 M0 · 계획서 부록 A.3) — std만 의존한다.
//
// 이 헤더는 `meta::`를 읽지도 확장하지도 않는다. 부록 A.1이 그렇게 정했다:
//   ① `meta::`는 Utility_Framework에 있어 Reflection.hpp를 타고 Player까지
//      퍼진다. 에디터 전용 어휘를 그쪽에 넣으면 CT3이 끊은 전파를 되돌린다.
//   ② `verify-reflection-golden.ps1`이 76타입 diff 0을 단정한다. `Meta::Method`를
//      건드리면 메뉴 작업이 이유 없이 그 게이트 안에 들어간다.
//   ③ `Meta::MakeMethod`는 포인터 투 멤버만 받는다(ReflectionFunction.h:277).
//      전역 동작을 원리적으로 표현할 수 없다.
// 물려받는 것은 관례 둘뿐이다 — 파일명 PascalCase, 네임스페이스 안 식별자
// snake_case(MetaSchema.h가 `meta::field`를 담는 그 분리).
//
// ── 왜 표면이 문자열이 아니라 닫힌 집합인가 ────────────────────────────────
//
// 상단 메뉴의 뿌리는 카테고리(File~Help)이고 팝업의 뿌리는 그리는 자리
// (Hierarchy·Content Browser…)다. **서로 다른 집합**이다. 둘을 한 경로 문자열에
// 담으면 뿌리 오타가 컴파일도 통과하고 실행도 통과한 채 **그려지지 않는 고아
// 항목**이 된다. 이 저장소가 반복해서 겪은 조용한 소멸이라, 뿌리는 열거형이고
// 문자열은 뿌리 뒤의 하위 경로만 맡는다.
//
// 그리고 **팝업 호스트가 동작의 인자 타입을 결정한다.** Hierarchy 팝업의 문맥은
// 클릭된 엔티티고 Content Browser 자산 팝업의 문맥은 파일 경로다. 이 차이를
// void*나 std::any로 뭉개면 CT6-a가 걷어낸 이중 타입소거가 새 소비자로 부활한다.
// 그래서 호스트마다 문맥 타입을 특성으로 못 박고 표기 단계에서 검사한다.

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <type_traits>

namespace editor
{
    // ── 표면 ──────────────────────────────────────────────────────────────

    // 상단 메뉴 카테고리. `count`는 열거와 배열의 어긋남을 잡는 보초다.
    enum class top_menu_root
    {
        file,
        edit,
        settings,
        tools,
        window,
        help,

        count,
    };

    // 팝업 호스트 = 팝업을 그리는 자리. 하나가 draw 지점 하나에 대응한다.
    //
    // ★ M1 에서 `scene_view` 를 뺐다. 계약(A.3)은 씬 뷰 팝업을 예정했는데, 배선하러
    //   가 보니 **그 자리가 없다.** 씬 뷰의 오른쪽 버튼은 카메라 시점 조작이다
    //   (`SceneViewWindow.cpp` 의 `IsMouseDown(Right)` → `EditorCameraRig::HandleMovement`,
    //   같은 버튼이 기즈모 단축키를 막는 변별자로도 쓰인다). 오른쪽 클릭 컨텍스트
    //   메뉴를 끼우면 카메라 조작과 다툰다.
    //
    //   게이트를 통과시키려고 UI 를 새로 만드는 것은 순서가 거꾸로다 — 열거자가
    //   먼저 있고 자리를 짜 맞추는 것이 아니라, 자리가 있어서 열거자가 있는 것이다.
    //   씬 뷰 컨텍스트 메뉴가 필요해지면 그것은 ViewportHost(W4·W5)가 입력 소유권을
    //   정리하며 할 결정이고, 그때 열거자 한 줄과 그리는 자리 한 곳을 더하면 된다.
    enum class popup_host
    {
        hierarchy,
        content_browser_folder,
        content_browser_asset,
        inspector_component,
        behavior_tree_node,
        animator_node,

        count,
    };

    inline constexpr std::array all_top_menu_roots{
        top_menu_root::file,
        top_menu_root::edit,
        top_menu_root::settings,
        top_menu_root::tools,
        top_menu_root::window,
        top_menu_root::help,
    };

    inline constexpr std::array all_popup_hosts{
        popup_host::hierarchy,
        popup_host::content_browser_folder,
        popup_host::content_browser_asset,
        popup_host::inspector_component,
        popup_host::behavior_tree_node,
        popup_host::animator_node,
    };

    // 열거자를 늘리고 배열을 안 늘리면 여기서 멈춘다. M2의 "배선 안 된 표면 0"
    // 게이트가 이 배열을 열거 원본으로 쓰므로, 누락은 게이트의 눈을 멀게 한다.
    static_assert(all_top_menu_roots.size() == static_cast<std::size_t>(top_menu_root::count),
        "all_top_menu_roots가 top_menu_root 전부를 담지 않는다");
    static_assert(all_popup_hosts.size() == static_cast<std::size_t>(popup_host::count),
        "all_popup_hosts가 popup_host 전부를 담지 않는다");

    constexpr const char* to_string(top_menu_root root) noexcept
    {
        switch (root)
        {
        case top_menu_root::file:     return "file";
        case top_menu_root::edit:     return "edit";
        case top_menu_root::settings: return "settings";
        case top_menu_root::tools:    return "tools";
        case top_menu_root::window:   return "window";
        case top_menu_root::help:     return "help";
        default:                      return "?";
        }
    }

    constexpr const char* to_string(popup_host host) noexcept
    {
        switch (host)
        {
        case popup_host::hierarchy:              return "hierarchy";
        case popup_host::content_browser_folder: return "content_browser_folder";
        case popup_host::content_browser_asset:  return "content_browser_asset";
        case popup_host::inspector_component:    return "inspector_component";
        case popup_host::behavior_tree_node:     return "behavior_tree_node";
        case popup_host::animator_node:          return "animator_node";
        default:                                 return "?";
        }
    }

    // 메뉴 라벨에 넣을 표시 이름. 상단 뿌리만 필요하다.
    constexpr const char* display_label(top_menu_root root) noexcept
    {
        switch (root)
        {
        case top_menu_root::file:     return "File";
        case top_menu_root::edit:     return "Edit";
        case top_menu_root::settings: return "Settings";
        case top_menu_root::tools:    return "Tools";
        case top_menu_root::window:   return "Window";
        case top_menu_root::help:     return "Help";
        default:                      return "?";
        }
    }

    // ── 대상 ──────────────────────────────────────────────────────────────
    //
    // ★ 전부 **신원을 든 값**이지 원시 포인터가 아니다. 메뉴 콜백은
    //   PresentationThread에서 도는데 실제 작업은 게임 스레드로 넘어가므로
    //   (부록 A.6), 큐를 한 번 거치는 사이 포인터가 죽을 수 있다. 엔티티는
    //   `@scene:index:generation` 문자열로 들고 다시 찾는다 — 공통 편집 계층이
    //   이미 그 신원을 쓴다(EditorObjectOperations::ObjectId).

    // 인자 없는 동작의 문맥. 빈 타입이라 저장 비용이 없고, 덕분에 상단 메뉴
    // 항목도 팝업과 같은 `void(*)(const Context&)` 한 모양으로 접힌다.
    struct global_target
    {
    };

    struct entity_target
    {
        std::string identity;   // @scene:index:generation
    };

    struct asset_target
    {
        std::filesystem::path path;
    };

    struct folder_target
    {
        std::filesystem::path path;
    };

    struct component_target
    {
        std::string entity_identity;
        std::string component;
    };

    // ── 호스트별 문맥 특성 ────────────────────────────────────────────────
    //
    // 특성화가 없는 호스트는 **컴파일 오류**다. 열거자를 늘리면 여기서 막힌다.

    template<popup_host Host>
    struct popup_context;

    template<> struct popup_context<popup_host::hierarchy>              { using type = entity_target; };
    template<> struct popup_context<popup_host::content_browser_folder> { using type = folder_target; };
    template<> struct popup_context<popup_host::content_browser_asset>  { using type = asset_target; };
    template<> struct popup_context<popup_host::inspector_component>    { using type = component_target; };
    template<> struct popup_context<popup_host::behavior_tree_node>     { using type = entity_target; };
    template<> struct popup_context<popup_host::animator_node>          { using type = entity_target; };

    template<popup_host Host>
    using popup_context_t = typename popup_context<Host>::type;
}
