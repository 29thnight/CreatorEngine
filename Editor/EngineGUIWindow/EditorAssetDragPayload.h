#pragma once
#include <filesystem>
#include <string_view>

struct ImGuiPayload;

// PHASE 21 W2-B — 콘텐츠 브라우저에서 끌어 온 자산의 **신원**.
//
// 예전 payload 는 파일 이름뿐이었고, 받는 자리 17곳이 저마다
// `PathFinder::Relative("<유형 폴더>\\") / 이름` 으로 경로를 다시 지었다. 전체 자산·
// 최근 항목에서는 유형 폴더 밖 파일도 끌 수 있어서 `Animation/Cha_Mon_5.fbx` 를
// 끌면 `Models/Cha_Mon_5.fbx` 가 열렸다. 이름은 신원이 아니다.
//
// 이제 payload 는 **UTF-8 전체 경로**다. 받는 자리는 경로를 그대로 쓴다.
// 저장 형식이 이름뿐이라 유형 폴더에서 다시 찾는 소비자(데칼 셋·스프라이트 시트)는
// `lives_in` 으로 그 폴더에 **바로** 있는 파일만 받고, 아니면 이유를 남기고 거부한다
// — 다른 파일로 조용히 바꿔 치우지 않는다. 폴리지는 stem 으로 GUID 를 찾으므로 그
// stem 이 끌어 온 파일로 돌아오는지를 따로 본다(ImGuiDrawHelperTerrainComponent.cpp).
//
// `DataSystem::LoadSharedTexture` 의 캐시 키도 stem 이었다가 경로로 바뀌었다(G2 —
// verify-texture-cache-identity.ps1). 전체 경로를 넘기면 그 파일이 돌아온다.
namespace editor::asset_drag
{
    /// 브라우저 타일이 부르는 유일한 입구. 경로 payload 표에 없는 타입은 싣지
    /// 않고 이유를 남긴다(EditorAssetDragPayload.cpp).
    void set_payload(const char* type, const std::filesystem::path& path);

    /// 이 payload 가 **경로를 나르는가**. 타입 이름으로만 답한다 — ImGui 는 payload 를
    /// 그것 하나로 구분한다. 특정 타입을 기다리는 자리는 `AcceptDragDropPayload` 가
    /// 이미 걸러 주므로 이 함수가 필요 없다; 필요한 쪽은 **타입을 모르고 조회하는**
    /// `GetDragDropPayload` 자리다(콘텐츠 브라우저 스냅샷). 거기서 "SCENE_OBJECT 만
    /// 아니면 경로" 로 읽다가 도킹 창 드래그의 `_IMWINDOW` 를 경로로 읽고 죽었다.
    bool carries_path(const ImGuiPayload& payload);

    /// payload 를 경로로 읽는다. 비었거나, 끝이 NUL 이 아니거나, 유효한 UTF-8 이
    /// 아니면 빈 경로 — 어느 경우에도 던지지 않는다.
    std::filesystem::path path_of(const ImGuiPayload& payload);

    /// `path` 가 `PathFinder::Relative(folder)` 바로 아래에 있는가. 아니면
    /// `consumer` 이름과 함께 오류를 남기고 거짓.
    bool lives_in(const std::filesystem::path& path, std::string_view folder, std::string_view consumer);
}
