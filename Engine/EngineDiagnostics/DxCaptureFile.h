#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "DxCaptureProtocol.h"

namespace ce::dx_capture
{
    // .ceprof의 버전이나 시계 해석을 바꾸지 않는 별도 원시 QPC 파일이다.
    inline constexpr std::uint32_t capture_file_version = 1;
    inline constexpr std::uint64_t maximum_file_bytes = 64ull * 1024ull * 1024ull;
    inline constexpr std::uint32_t maximum_file_records = 262144;
    inline constexpr std::uint32_t no_record = UINT32_MAX;

    enum class file_error : std::uint8_t
    {
        open_failed,
        read_failed,
        write_failed,
        not_a_capture,
        unsupported_version,
        truncated,
        checksum_mismatch,
        resource_limit,
        malformed,
        invalid_sequence,
        invalid_qpc
    };

    const char* describe(file_error error);

    struct recording_limits
    {
        std::uint64_t max_file_bytes = maximum_file_bytes;
        std::uint32_t max_records = maximum_file_records;
    };

    enum recording_issue : std::uint32_t
    {
        no_issues = 0,
        source_loss = 1u << 0,
        source_error = 1u << 1,
        source_incomplete = 1u << 2,
        record_limit = 1u << 3,
        missing_footer = 1u << 4,
        truncated_record = 1u << 5,
        missing_stop = 1u << 6,
        unfinished_execution = 1u << 7,
        caller_incomplete = 1u << 8,
        writer_error = 1u << 9
    };

    struct recording_summary
    {
        std::uint64_t valid_bytes = 0;
        std::uint64_t file_bytes = 0;
        std::uint64_t dropped_records = 0;
        // 원본 status가 보고한 누적 손실의 최댓값이다. ETW/IPC별 완전한 합계라는 뜻이 아니다.
        std::uint64_t reported_loss_count = 0;
        std::uint64_t etw_events_lost = 0;
        std::uint64_t etw_buffers_lost = 0;
        std::uint64_t first_qpc = 0;
        std::uint64_t last_qpc = 0;
        std::uint32_t record_count = 0;
        std::uint32_t execution_count = 0;
        std::uint32_t work_count = 0;
        std::uint32_t submission_count = 0;
        std::uint32_t unmatched_executions = 0;
        std::uint32_t ambiguous_executions = 0;
        std::uint32_t issues = no_issues;
        status_code last_source_status = status_code::none;
        bool source_stopped = false;
        bool finalized = false;
        bool complete = false;
    };

    enum class association_state : std::uint8_t
    {
        unmatched,
        unique,
        ambiguous
    };

    struct execution_link
    {
        std::uint32_t begin_record = no_record;
        std::uint32_t end_record = no_record;
        std::uint32_t submission_record = no_record;
        association_state association = association_state::unmatched;
        bool duplicate_execution = false;
    };

    struct work_link
    {
        std::uint32_t work_record = no_record;
        std::uint32_t execution_index = no_record;
        std::uint32_t submission_record = no_record;
        association_state association = association_state::unmatched;
    };

    class recording_snapshot
    {
    public:
        const record& session() const { return records_.front(); }
        std::span<const record> records() const { return records_; }
        std::span<const execution_link> execution_links() const { return executions_; }
        std::span<const work_link> work_links() const { return works_; }
        const recording_summary& summary() const { return summary_; }

    private:
        std::vector<record> records_;
        std::vector<execution_link> executions_;
        std::vector<work_link> works_;
        recording_summary summary_;

        friend struct recording_decoder;
    };

    using recording_snapshot_ptr = std::shared_ptr<const recording_snapshot>;

    // 파일/인덱스 구성은 UI 밖에서 끝낸 뒤 const 스냅샷만 UI에 넘긴다.
    // 푸터 없는 유효 접두 구간은 복구하지만 complete로 승격하지 않는다.
    std::expected<recording_snapshot_ptr, file_error>
    decode_recording(std::span<const std::byte> bytes, recording_limits limits = {});

    std::expected<recording_snapshot_ptr, file_error>
    read_recording(const std::filesystem::path& path, recording_limits limits = {});

    // 합성 fixture와 작은 내보내기용. records[0]은 session이어야 한다.
    std::expected<std::vector<std::byte>, file_error>
    encode_recording(std::span<const record> records, bool source_stopped_cleanly,
                     recording_limits limits = {});

    class recording_writer
    {
    public:
        // 일반 권한 Editor의 백그라운드 작업자만 소유한다. helper에 경로를 넘기지 않는다.
        // 이미 있는 파일은 덮어쓰지 않는다. session을 첫 레코드로 즉시 기록한다.
        static std::expected<std::unique_ptr<recording_writer>, file_error>
        open(const std::filesystem::path& path, const record& session, recording_limits limits = {});
        ~recording_writer();
        recording_writer(const recording_writer&) = delete;
        recording_writer& operator=(const recording_writer&) = delete;

        // 동기 스트리밍 API다. 렌더/UI/ETW callback에서 호출하지 않는다.
        // 제한을 넘은 레코드는 거절하고 손실을 푸터에 남긴다. 무손실을 약속하지 않는다.
        bool append(const record& value);
        std::expected<void, file_error> finalize(bool source_stopped_cleanly);
        const recording_summary& status() const;

    private:
        struct implementation;
        explicit recording_writer(std::unique_ptr<implementation> implementation);
        std::unique_ptr<implementation> implementation_;
    };
}
