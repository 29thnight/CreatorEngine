#pragma once

// PHASE 21 W7 — Content Browser 비동기 에셋 썸네일.
//
// 계약은 `docs/analysis/EditorIconSelectionStudy.md` 의 "Content Browser 비동기
// 썸네일 계약" 이다. 다섯 상태(미요청·로딩·Ready·실패·무효화)와 가시 타일 우선,
// 중복 억제, 늦은 완료 폐기, 메모리 예산·퇴출을 여기서 든다.
//
// ── 왜 새 장부를 여는가 ──
//
// 계약의 완료 판정은 *"느린 로더에서 아이콘 상태로도 UI 입력이 처리되는지,
// 동일 자산 요청이 중복되지 않는지, 변경·삭제 뒤 늦은 완료가 재게시되지
// 않는지"* 다. 셋 다 **수를 세지 않으면 판정할 수 없다** — 화면만 봐서는
// 중복 요청도, 폐기된 늦은 완료도 보이지 않는다. W7-0 의 `panel_cost` 가 시간과
// 스캔을 세듯, 이 조각은 **요청의 일생**을 센다.
//
// ── 스레드 경계 ──
//
//   작업 스레드(WorkerPool)  : 파일 읽기 · CPU 디코딩 · 축소까지. **여기까지다.**
//   Presentation 스레드      : Texture 생성 · 업로드 예약 · 게시 · 타일 조회.
//   게임 스레드(CLI)         : 장부 읽기/리셋.
//
// ★ 작업 스레드는 `Texture` 를 만들지 않는다. 계약이 *"작업 스레드가 ImGui나
//   렌더 문맥을 직접 호출하지 않는다"* 고 적었고, `Texture` 생성은 텍스처 캐시의
//   신원(`m_assetId`)을 발급해 셸의 디스크립터 표와 엮이는 자리다. 작업 결과는
//   **RGBA8 바이트 묶음**으로만 돌아오고, 그것을 `Texture` 로 세우는 것은 프레임
//   머리의 Presentation 스레드다.
//
// ── Ready 의 뜻 ──
//
// ★ "CPU 이미지가 생겼다" 는 Ready 가 아니다. 계약이 *"실제 GPU 사용 가능
//   시점을 따른다"* 고 적은 이유는 `RegisterTexture` 의 반환값이 그것을 말하지
//   않기 때문이다 — DX12 는 표시 프레임 밖 호출에서 슬롯만 잡고 null SRV 를 써
//   두므로 업로드 전에도 0 이 아닌 ID 가 나온다. 그 ID 로 그리면 한 프레임
//   **빈 그림**이 나온다. 그래서 상태가 하나 더 있다(`awaiting_upload`):
//   Texture 는 섰지만 아직 안 올라간 구간이다.
//
//   그 구간에서 `EditorImGuiTexture::Prime` 으로 **그리지 않고 등록만** 한다.
//   이것이 없으면 교착이다 — 준비될 때까지 아이콘만 그리는 타일은 썸네일
//   텍스처를 한 번도 등록하지 않고, 등록되지 않은 텍스처는 영원히 안 올라간다.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

class Texture;

namespace editor
{
    namespace thumbnail_fs = std::filesystem;

    /// 지금 이 조각이 아는 생성기. 버전을 키에 넣는 이유는 생성 방식이 바뀌면
    /// 옛 그림을 그대로 쓰면 안 되기 때문이다(계약의 "생성 방식 버전").
    enum class thumbnail_generator : std::uint16_t
    {
        none = 0,
        /// 이미지 파일을 디코드해 축소한다. `Texture::DecodeToRgba8` 이 판다.
        texture = 1,
        // model = 2 — 형상을 렌더한다. 오프스크린 패스가 필요해 이 조각 밖이다.
    };

    /// 캐시 키.
    ///
    /// ★ 계약은 *"자산 GUID 와 subasset 신원, 원본/의존성 revision"* 으로 적었다.
    ///   여기서는 **경로 해시 + 원본 변경 표식**을 쓴다. 이유:
    ///
    ///   자산 GUID 는 `.meta` 를 읽어야 나온다. 그것을 타일마다 하면 W7-5 가
    ///   방금 없앤 "프레임마다 디스크를 만진다" 가 그대로 돌아온다. 반면
    ///   마지막 쓰기 시각과 크기는 목록 스캔이 **이미 들고 있는 값**이라
    ///   (`directory_iterator` 가 찾기 결과에 담아 준다) 새 접촉이 0 이다.
    ///
    ///   물음은 같다 — *"다른 파일이나 구버전 그림이 나타나지 않게"*. 경로가
    ///   신원이고 (쓰기 시각 ^ 크기) 가 revision 이다. 오히려 revision 을 안
    ///   올리고 내용만 바뀐 편집도 이 표식은 잡는다.
    ///
    ///   맞바꾼 것: 파일을 **이름만 바꾸면** 키가 달라져 멀쩡한 그림을 한 번 더
    ///   만든다. GUID 였다면 살아남았다. 한 번의 재디코딩이고, 그 대가로 매
    ///   프레임의 디스크 접촉이 0 이다.
    ///
    ///   subasset 자리는 지금 0 이다 — 지금 생성기(texture)는 파일 하나가 그림
    ///   하나다. 모델 생성기가 오면 그때 채운다.
    struct thumbnail_key
    {
        std::uint64_t path{};      ///< 경로 문자열 해시
        std::uint64_t revision{};  ///< 마지막 쓰기 시각 ^ 파일 크기
        std::uint32_t subasset{};  ///< 컨테이너 안의 몇 번째인가(지금은 0)
        std::uint16_t resolution{};///< 요청 해상도(정사각 한 변)
        thumbnail_generator generator{ thumbnail_generator::none };

        bool operator==(const thumbnail_key&) const noexcept = default;
    };

    /// 경로와 목록 스캔이 담아 둔 변경 표식으로 키를 만든다. 디스크를 만지지
    /// 않는다 — 인자로 받은 값만 쓴다.
    thumbnail_key thumbnail_make_key(const thumbnail_fs::path& source,
                                     std::uint64_t revision,
                                     std::uint16_t resolution) noexcept;

    /// 확장자로 생성기를 고른다. 모르는 것은 `none` 이고, 그 타일은 유형
    /// 아이콘에 머문다(계약의 "미지원 생성기는 아이콘을 유지한다").
    thumbnail_generator thumbnail_pick_generator(std::string_view extension) noexcept;

    /// 요청 하나의 일생. CLI 와 게이트가 이 철자를 쓴다.
    enum class thumbnail_state : std::uint8_t
    {
        queued = 0,       ///< 등록만 됐다. 아직 작업자가 안 잡았다
        working,          ///< 작업 스레드가 파일을 읽고 디코드하는 중
        awaiting_upload,  ///< CPU 픽셀이 왔고 Texture 는 섰다. GPU 업로드 대기
        ready,            ///< 올라갔다. 이 프레임부터 타일이 이 그림을 받는다
        failed,           ///< 읽기·디코딩 실패. 재요청하지 않는다
        count
    };

    const char* thumbnail_state_name(thumbnail_state state) noexcept;

    struct thumbnail_stats
    {
        // ── 요청의 일생(누계) ──
        std::uint64_t requests{};     ///< 새 키로 등록한 요청
        std::uint64_t deduped{};      ///< 이미 있는 키라 등록하지 않은 조회
        std::uint64_t decoded{};      ///< 작업이 CPU 픽셀을 낸 수
        std::uint64_t published{};    ///< Ready 로 승격한 수
        std::uint64_t failed{};       ///< 읽기·디코딩 실패
        /// ★ 세대가 어긋나 **버린** 완료 수. 계약이 *"오래된 요청의 늦은 완료는
        ///   폐기한다"* 고 적은 그것이다. 이 수를 세지 않으면 늦은 완료가
        ///   재게시되는지 알 방법이 없다.
        std::uint64_t lateDropped{};
        std::uint64_t invalidated{};  ///< 변경·삭제로 버린 항목
        std::uint64_t evicted{};      ///< 예산 초과로 퇴출한 항목

        // ── 타일이 무엇을 받았는가(누계) ──
        std::uint64_t servedThumbnails{}; ///< 썸네일을 받은 조회
        std::uint64_t servedIcons{};      ///< 아이콘에 머문 조회

        // ── 지금 상태 ──
        std::size_t   entries{};      ///< 표에 있는 항목 수
        std::size_t   queued{};       ///< 대기 중
        std::size_t   working{};      ///< 작업 중
        std::size_t   awaitingUpload{};
        std::size_t   ready{};
        std::uint64_t bytes{};        ///< 지금 쥔 CPU 픽셀 바이트
        std::uint64_t budgetBytes{};  ///< 예산

        /// 작업자 풀이 서 있는가. ★ `WorkerPool` 은 풀이 없으면 **그 자리에서**
        /// 작업을 실행한다 — 그러면 디코딩이 UI 스레드에서 돌아 계약이 금지한
        /// "UI 가 완료를 기다린다" 가 된다. 그래서 풀이 설 때까지 등록만 하고
        /// 넘기지 않으며, 이 값이 그 사실을 밖에 낸다.
        bool workerPoolRunning{};
    };

    // ── Presentation 스레드 전용 ────────────────────────────────────────────

    /// UI 프레임 머리에서 한 번. 완료 큐를 반영하고(작업 결과 → Texture),
    /// 올라간 것을 Ready 로 **승격**하며, 예산을 넘으면 퇴출한다.
    ///
    /// ★ 승격이 여기 있는 이유가 계약의 *"게시된 다음 프레임부터 교체"* 다.
    ///   프레임 안에서 승격하면 같은 프레임의 앞뒤 타일이 서로 다른 그림을
    ///   받는다. 프레임 머리에서 한 번 바꾸면 그 프레임 전체가 한 판이다.
    void thumbnail_begin_frame();

    /// 타일 하나가 그릴 그림을 묻는다.
    ///
    /// Ready 면 그 텍스처, 아니면 **nullptr**(호출자는 유형 아이콘을 그린다).
    /// 처음 보는 키면 요청을 등록한다. 같은 키를 한 프레임에 여러 번 물어도
    /// 작업은 한 번만 돈다(`deduped` 가 오른다).
    ///
    /// `visible` 은 지금 화면에 보이는 타일인가다 — 계약의 "가시 타일 우선
    /// 요청". clipping 이 건너뛴 행은 묻지 않으므로 사실상 전부 참이지만,
    /// 목록이 그것을 보장하지 않으므로 인자로 받는다.
    Texture* thumbnail_acquire(const thumbnail_key& key,
                               const thumbnail_fs::path& source,
                               bool visible);

    /// 이 경로의 항목을 버린다. 자산이 바뀌거나 지워졌을 때 부른다.
    /// 진행 중인 작업의 결과는 세대가 어긋나 **폐기**된다(`lateDropped`).
    void thumbnail_invalidate(const thumbnail_fs::path& source);

    /// 전부 버린다. 폴더를 옮기거나 프로젝트가 바뀔 때.
    void thumbnail_invalidate_all();

    /// 종료. 진행 중인 작업의 결과가 돌아올 자리를 닫고 표를 비운다.
    void thumbnail_shutdown();

    // ── 게임 스레드(CLI) ────────────────────────────────────────────────────
    thumbnail_stats thumbnail_read_stats();
    void thumbnail_reset_stats();
}
