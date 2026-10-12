#pragma once
// PHASE 14 P6 — `.ceprof` 캡처 파일(§8).
//
// 얼린 캡처를 파일로 내보내고 다시 연다. 지금까지 얼린 캡처는 프로세스와 함께
// 사라졌다 — 무엇을 재든 **두 번 볼 수가 없었다.** 멀티카메라 GPU 매핑, 네 모드
// 오버헤드 비교(§11.4), counter 정렬이 전부 "두 번 봐야" 증명되는 일이다.
//
// ★ 파일은 캡처가 **들고 있는 것만** 싣는다. 이름(capture_marker)과 시계
//   (capture_environment)를 캡처가 먼저 소유하게 한 것이 P6-1 이다. 그러지
//   않았으면 이 파일은 남의 빌드의 id 와 읽는 기계의 주파수로 풀려, 틀린
//   이름과 틀린 길이를 그럴듯하게 그렸을 것이다.
//
// Snapshot v1/v2/v4/v6 (little-endian); continuous recording v3/v5/v7 below.
//
//   header      magic "CEPROF\0\0" · u32 format_version · u32 chunk_count
//   chunk table chunk_count × { u32 type · u32 version · u64 offset · u64 size ·
//                               u32 crc32 · u32 reserved }
//   chunks      environment · markers · threads · frames · counters(optional)
//               · render_measurements (v4: chunk 7 version 1; v6: version 2)
//   counter chunk v1: id(u16)/value(f64); v2 adds CPU session/tick/task (3*u64).
//
// ★ 이벤트는 **필드 하나씩** 쓴다. `profile_event` 를 통째로 복사하면 구조체의
//   패딩과 배치가 곧 파일 형식이 되는데, 그 구조체는 "크기가 바뀌면 이 줄을
//   고쳐라" 는 static_assert 를 달고 있다 — 레코드를 넓히는 순간 모든 옛 파일이
//   **조용히 틀린 값으로** 읽힌다. 필드별로 쓰면 넓혀도 옛 파일은 옛 뜻 그대로다.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

#include "ProfileCapture.h"

namespace ce
{
    // Snapshot v6 adds CPU presenter-return evidence; event layout stays v2.
    // Versions 3/5/7 are continuous streams, never snapshots.
    inline constexpr std::uint32_t kCaptureFileVersion = 6;
    // v1 events: 38 wire bytes. v2 frames append three u64 CPU ownership IDs (62 bytes).
    // v2 thread vocabulary adds physics_worker; existing track values stay unchanged.
    // Native producer pages are version 3 / 64-byte events plus a bounded
    // render-measurement sidecar, independent of the file layout. Snapshot v1/v2
    // and continuous v3 decode without measurements: unknown stays unknown.
    // Snapshot chunk7v1: u32 frame count, then (u32 engine frame, u32 sample
    // count, count * 100-byte profile_render_measurement). Continuous v5 appends
    // u32 sample count + the same records after each frame's old events/counters.
    // Snapshot v6/chunk7v2 and continuous v7 admit axis 3 (CPU presenter return).
    // The tagged record remains 100 bytes; v4/v5 admit only old axes 1/2.

    // 왜 못 읽었는가. ★ 하나로 뭉치지 않는다 — "파일이 잘렸다" 와 "누가 고쳤다"
    // 와 "더 새 빌드가 썼다" 는 사용자가 할 일이 다르다.
    enum class capture_file_error : std::uint8_t
    {
        open_failed,          // 파일을 열 수 없다
        write_failed,         // 쓰다가 실패했다(디스크·권한). 기존 파일은 그대로다
        not_a_capture,        // 매직이 다르다 — .ceprof 가 아니다
        unsupported_version,  // 이 빌드보다 새 형식이다
        truncated,            // 선언한 것보다 짧다 — 쓰다 끊긴 파일
        checksum_mismatch,    // 청크의 CRC 가 안 맞는다 — 손상
        resource_limit,       // 해석할 구간이 지정한 메모리 한도를 넘는다
        malformed,            // 크기·개수·순서가 앞뒤가 안 맞는다
        canceled,             // Viewer canceled at a record/chunk boundary; source is unchanged
    };

    // 사람이 읽을 한 줄. 오류 창과 로그가 쓴다.
    const char* describe(capture_file_error error);

    // ── 메모리 안에서 ──────────────────────────────────────────────────────
    //
    // 게이트가 **디스크 없이** 절단과 손상을 자극하는 자리다. 바이트 열을 한
    // 칸씩 잘라 가며 decode 에 넣으면 "어디서 끊겨도 멈추지 않는다" 를 전수로
    // 증명할 수 있다 — 파일로는 그 수만큼 디스크를 써야 한다.
    std::vector<std::byte> encode_capture(const capture_session& capture);

    std::expected<capture_session_ptr, capture_file_error>
    decode_capture(std::span<const std::byte> bytes);

    // IPC entry points preflight allocation/count/work limits before calling the
    // versioned codec. No partial snapshot is presented as a complete one.
    std::expected<std::vector<std::byte>, capture_file_error>
    encode_capture_bounded(const capture_session& capture, std::size_t max_wire_bytes);
    std::expected<capture_session_ptr, capture_file_error>
    decode_capture_bounded(std::span<const std::byte> bytes, std::size_t max_wire_bytes);

    // ── 파일 ───────────────────────────────────────────────────────────────
    //
    // ★ 저장은 **임시 파일에 완성한 뒤 교체**한다(§8.2). 쓰다가 실패해도 같은
    //   이름의 기존 파일은 깨지지 않는다.
    //
    // ★ 받는 것이 `const capture_session&` 이다. 얼린 캡처는 아무도 고치지
    //   않으므로 저장하는 동안 새 녹화가 시작돼도 **저장 대상이 변하지 않는다**
    //   — 완료 조건 넷째가 잠금이 아니라 자료구조로 선다.
    std::expected<void, capture_file_error>
    save_capture(const capture_session& capture, const std::filesystem::path& path);

    std::expected<capture_session_ptr, capture_file_error>
    load_capture(const std::filesystem::path& path);
    // 연속 녹화는 600프레임 UI 캐시와 별개인 추가 전용 스트림을 쓴다.
    inline constexpr std::uint32_t kRecordingFileVersion = 7;

    enum class recording_state : std::uint8_t
    {
        starting,
        recording,
        flushing,
        finalized,
        failed,
    };

    struct recording_options
    {
        std::size_t max_queued_bytes = 64ull * 1024ull * 1024ull;
        std::uint32_t max_queued_batches = 64;
    };

    struct recording_source_losses
    {
        std::uint64_t dropped_events = 0;
        std::uint64_t dropped_frame_boundaries = 0;
        std::uint64_t late_events = 0;
        std::uint64_t late_gpu_spans = 0;
    };

    // written은 운영체제에 전달한 바이트, flushed는 C++ 스트림 버퍼를 비운 경계다.
    // 물리 디스크 영속성 보장이 아니며, 아직 링에 있는 꼬리는 충돌 시 없을 수 있다.
    struct recording_status
    {
        recording_state state = recording_state::starting;
        std::uint64_t queued_bytes = 0;
        std::uint64_t queued_batches = 0;
        std::uint64_t written_bytes = 0;
        std::uint64_t flushed_bytes = 0;
        std::uint64_t written_frames = 0;
        std::uint64_t submitted_frames = 0;
        profile_tick first_tick = 0;
        profile_tick last_tick = 0;
        std::uint64_t dropped_frames = 0;
        std::uint64_t dropped_events = 0;
        std::uint64_t dropped_counters = 0;
        std::uint64_t source_dropped_counters = 0;
        recording_source_losses source_losses;
        std::optional<capture_file_error> error;
    };

    class continuous_capture_writer
    {
    public:
        // 빈 경로는 복구 가능한 고유 임시 녹화를 만든다. 파일 쓰기는 전용 작업자가 맡는다.
        static std::expected<std::unique_ptr<continuous_capture_writer>, capture_file_error>
        start(const std::filesystem::path& path = {}, recording_options options = {});
        ~continuous_capture_writer();
        continuous_capture_writer(const continuous_capture_writer&) = delete;
        continuous_capture_writer& operator=(const continuous_capture_writer&) = delete;

        // 수집기 전용. 프레임은 최종 상태·엄격한 오름차순이며, 접수 뒤 변하지 않아야 한다.
        // 디스크나 큐의 빈자리를 기다리지 않는다. 거절한 묶음은 상태와 푸터에 센다.
        bool append(capture_session_ptr batch);
        // Stop 꼬리 배출용. 수집기만 큐의 빈자리를 기다리며, 쓰기 실패 시 즉시 깨어난다.
        bool append_final(capture_session_ptr batch);
        void request_flush();
        void request_finalize(bool complete, std::uint32_t unacked_streams,
                              std::uint64_t dropped_counters = 0, recording_source_losses source_losses = {});
        recording_status status() const;
        const std::filesystem::path& path() const;
        // 완료를 기다린다. 수집기·백그라운드 작업자 전용이며 UI·생산자에서는 호출하지 않는다.
        void wait();

    private:
        bool append_impl(capture_session_ptr batch, bool wait_for_capacity);
        struct implementation;
        explicit continuous_capture_writer(std::unique_ptr<implementation> implementation);
        std::unique_ptr<implementation> implementation_;
    };

    struct recording_overview_bin
    {
        std::uint64_t first_ordinal = 0;
        std::uint64_t frame_count = 0;
        std::uint32_t first_engine_frame = 0;
        std::uint32_t last_engine_frame = 0;
        profile_tick tick_begin = 0;
        profile_tick tick_end = 0;
        profile_tick minimum_duration = 0;
        profile_tick maximum_duration = 0;
        std::uint64_t event_count = 0;
        std::uint64_t dropped_events = 0;
    };

    class capture_recording
    {
    public:
        std::uint64_t frame_count() const;
        std::span<const recording_overview_bin> overview() const;
        const std::filesystem::path& path() const;
        capture_session_ptr metadata() const;
        bool finalized() const;
        bool recovered() const;
        bool complete() const;
        recording_status status() const;
        std::uint64_t valid_bytes() const;

        // 순번은 엔진 프레임 ID와 별개인 녹화 내 순서다. 선택 구간만 해석하며
        // 이벤트·카운터·프레임 소유 메모리에 한도를 둔다. 레코드 입력 버퍼는 최대 32MiB다.
        std::expected<capture_session_ptr, capture_file_error>
        load_range(std::uint64_t first_ordinal, std::uint32_t count,
                   std::size_t max_bytes = kDefaultMemoryBudget, std::stop_token cancel = {}) const;

    private:
        struct implementation;
        explicit capture_recording(std::shared_ptr<implementation> implementation);
        std::shared_ptr<implementation> implementation_;
        friend std::expected<std::shared_ptr<const capture_recording>, capture_file_error>
        open_capture_recording(const std::filesystem::path& path, std::stop_token cancel);
    };

    using capture_recording_ptr = std::shared_ptr<const capture_recording>;

    // 제한된 작업 메모리와 최대 2048개 개요 구간으로 읽는다. 잘린 마지막 레코드는
    // 복구하고, 완성된 레코드의 구조·CRC 오류는 거절한다. v1/v2의 이벤트 값은 구간을 읽을 때 검증한다.
    std::expected<capture_recording_ptr, capture_file_error>
    open_capture_recording(const std::filesystem::path& path, std::stop_token cancel = {});

    // 검증한 접두 구간만 같은 디렉터리의 임시 파일을 거쳐 복사한다. 원본은 복구용으로 남긴다.
    std::expected<void, capture_file_error>
    save_recording(const capture_recording& recording, const std::filesystem::path& path,
                   std::stop_token cancel = {});
}
