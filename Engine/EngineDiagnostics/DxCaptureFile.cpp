#include "DxCaptureFile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <tuple>
#include <utility>

// 유니티 빌드에서 다른 파일의 내부 이름과 섞이지 않도록 전용 이름공간을 쓴다.
namespace ce::dx_capture::file_detail
{
    static_assert(std::endian::native == std::endian::little);
    constexpr std::array<char, 8> magic{ 'C', 'E', 'D', 'X', 'R', 'A', 'W', '\0' };
    constexpr std::uint32_t record_tag = 0x43455244; // DREC
    constexpr std::uint32_t footer_tag = 0x444E4544; // DEND
    constexpr std::uint32_t envelope_version = 1;
    constexpr std::size_t header_bytes = 64;
    constexpr std::size_t envelope_bytes = 32;
    constexpr std::size_t footer_payload_bytes = 48;
    constexpr std::size_t footer_bytes = envelope_bytes + footer_payload_bytes;
    constexpr std::size_t record_bytes = envelope_bytes + sizeof(record);
    constexpr std::uint32_t persisted_issues = source_loss | source_error | source_incomplete |
        record_limit | caller_incomplete | writer_error | unfinished_execution;

    template<class T>
    void put(std::span<std::byte> bytes, std::size_t offset, T value)
    {
        static_assert(std::is_integral_v<T>);
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    template<class T>
    T get(std::span<const std::byte> bytes, std::size_t offset)
    {
        static_assert(std::is_integral_v<T>);
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    std::uint32_t update_crc(std::uint32_t crc, std::span<const std::byte> bytes)
    {
        for (const auto value : bytes)
        {
            crc ^= std::to_integer<std::uint8_t>(value);
            for (int bit = 0; bit != 8; ++bit)
            {
                crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0xEDB88320u : 0u);
            }
        }
        return crc;
    }

    std::uint32_t crc32(std::span<const std::byte> bytes)
    {
        return update_crc(UINT32_MAX, bytes) ^ UINT32_MAX;
    }

    bool valid_limits(const recording_limits& limits)
    {
        return limits.max_records != 0 && limits.max_records <= maximum_file_records &&
            limits.max_file_bytes >= header_bytes + record_bytes + footer_bytes &&
            limits.max_file_bytes <= maximum_file_bytes;
    }

    std::expected<void, file_error> validate_record(const record& value, const record* session)
    {
        if (value.qpc_end != 0 && value.qpc_end < value.qpc_begin)
        {
            return std::unexpected(file_error::invalid_qpc);
        }
        if (!valid_record(value))
        {
            return std::unexpected(file_error::malformed);
        }
        if (value.kind == record_kind::session)
        {
            if (session != nullptr || value.process_id == 0)
            {
                return std::unexpected(file_error::malformed);
            }
            if (value.qpc_begin == 0 || value.qpc_frequency == 0 ||
                value.qpc_frequency > static_cast<std::uint64_t>(INT64_MAX))
            {
                return std::unexpected(file_error::invalid_qpc);
            }
        }
        else
        {
            if (session == nullptr ||
                (value.kind != record_kind::status && value.process_id == 0) ||
                (value.process_id != 0 && value.process_id != session->process_id))
            {
                return std::unexpected(file_error::malformed);
            }
            if (value.qpc_frequency != 0 && value.qpc_frequency != session->qpc_frequency)
            {
                return std::unexpected(file_error::invalid_qpc);
            }
        }
        if (value.qpc_begin > static_cast<std::uint64_t>(INT64_MAX) ||
            value.qpc_end > static_cast<std::uint64_t>(INT64_MAX) ||
            value.cpu_submit_qpc > static_cast<std::uint64_t>(INT64_MAX) ||
            value.source_begin_ns > static_cast<std::uint64_t>(INT64_MAX) ||
            value.source_end_ns > static_cast<std::uint64_t>(INT64_MAX) ||
            value.source_cpu_submit_ns > static_cast<std::uint64_t>(INT64_MAX) ||
            ((value.flags & has_cpu_submit) != 0 && value.cpu_submit_qpc == 0))
        {
            return std::unexpected(file_error::invalid_qpc);
        }
        if (value.kind == record_kind::submission &&
            (value.qpc_begin == 0 || value.qpc_end < value.qpc_begin))
        {
            return std::unexpected(file_error::invalid_qpc);
        }
        // begin은 CPU 제출만 알고 GPU 시각은 아직 모르는 정상 레코드다.
        // CPU 시각을 GPU 시작으로 대신 채우지 않는다.
        if (value.kind == record_kind::gpu_work)
        {
            const bool valid_qpc_interval = value.qpc_begin != 0 && value.qpc_end >= value.qpc_begin;
            const bool raw_only = value.qpc_begin == 0 && value.qpc_end == 0 &&
                value.source_begin_ns != 0 && value.source_end_ns >= value.source_begin_ns &&
                (value.flags & incomplete) != 0 && (value.flags & calibrated_qpc) == 0;
            if (!valid_qpc_interval && !raw_only)
            {
                return std::unexpected(file_error::invalid_qpc);
            }
        }
        if ((value.source_begin_ns != 0 || value.source_end_ns != 0) &&
            value.kind != record_kind::gpu_execution_begin &&
            (value.source_begin_ns == 0 || value.source_end_ns < value.source_begin_ns))
        {
            return std::unexpected(file_error::invalid_qpc);
        }
        if (value.kind == record_kind::gpu_execution_end && value.qpc_begin != 0 && value.qpc_end == 0)
        {
            return std::unexpected(file_error::invalid_qpc);
        }
        if (value.kind == record_kind::gpu_execution_end && value.qpc_begin == 0 && value.qpc_end == 0 &&
            value.source_begin_ns == 0 && value.source_end_ns == 0 && (value.flags & incomplete) == 0)
        {
            return std::unexpected(file_error::invalid_qpc);
        }
        return {};
    }

    void observe(recording_summary& summary, const record& value)
    {
        ++summary.record_count;
        summary.execution_count += value.kind == record_kind::gpu_execution_begin ? 1u : 0u;
        summary.work_count += value.kind == record_kind::gpu_work ? 1u : 0u;
        summary.submission_count += value.kind == record_kind::submission ? 1u : 0u;
        summary.reported_loss_count = (std::max)(summary.reported_loss_count, value.loss_count);
        summary.etw_events_lost = (std::max)(summary.etw_events_lost, value.etw_events_lost);
        summary.etw_buffers_lost = (std::max)(summary.etw_buffers_lost, value.etw_buffers_lost);
        for (const auto qpc : { value.qpc_begin, value.qpc_end, value.cpu_submit_qpc })
        {
            if (qpc != 0)
            {
                summary.first_qpc = summary.first_qpc == 0 ? qpc : (std::min)(summary.first_qpc, qpc);
                summary.last_qpc = (std::max)(summary.last_qpc, qpc);
            }
        }
        if ((value.flags & (transport_loss | etw_loss)) != 0 || value.loss_count != 0 ||
            value.etw_events_lost != 0 || value.etw_buffers_lost != 0 ||
            value.status == status_code::events_lost)
        {
            summary.issues |= source_loss;
        }
        if ((value.flags & incomplete) != 0)
        {
            summary.issues |= source_incomplete;
        }
        if (value.kind == record_kind::status)
        {
            if (summary.source_stopped && (value.status == status_code::ready ||
                value.status == status_code::capturing))
            {
                summary.issues |= source_error;
            }
            summary.last_source_status = value.status;
            summary.source_stopped = summary.source_stopped || value.status == status_code::stopped;
            if (value.status == status_code::permission_denied || value.status == status_code::unsupported ||
                value.status == status_code::provider_failure || value.status == status_code::malformed_event ||
                value.status == status_code::disconnected)
            {
                summary.issues |= source_error;
            }
        }
    }

    std::array<std::byte, header_bytes> make_header(const record& session)
    {
        std::array<std::byte, header_bytes> result{};
        std::memcpy(result.data(), magic.data(), magic.size());
        put<std::uint32_t>(result, 8, capture_file_version);
        put<std::uint32_t>(result, 12, header_bytes);
        put<std::uint64_t>(result, 16, session.qpc_frequency);
        put<std::uint64_t>(result, 24, session.qpc_begin);
        put<std::uint64_t>(result, 32, session.process_creation_time);
        put<std::uint32_t>(result, 40, session.process_id);
        put<std::uint32_t>(result, 44, protocol_version);
        put<std::uint32_t>(result, 48, sizeof(record));
        put<std::uint32_t>(result, 60, crc32(std::span(result).first(60)));
        return result;
    }

    template<std::size_t N>
    void finish_envelope(std::array<std::byte, N>& bytes, std::uint32_t tag, std::uint64_t sequence)
    {
        put<std::uint32_t>(bytes, 0, tag);
        put<std::uint32_t>(bytes, 4, envelope_version);
        put<std::uint64_t>(bytes, 8, sequence);
        put<std::uint32_t>(bytes, 16, N - envelope_bytes);
        const auto prefix_crc = update_crc(UINT32_MAX, std::span(bytes).first(20));
        put<std::uint32_t>(bytes, 20,
            update_crc(prefix_crc, std::span(bytes).subspan(envelope_bytes)) ^ UINT32_MAX);
    }

    std::array<std::byte, record_bytes> make_record(const record& value, std::uint64_t sequence)
    {
        std::array<std::byte, record_bytes> result{};
        // protocol v1은 필드 위치/크기/예약 바이트가 고정된 little-endian POD다.
        // 파일 머리에 그 버전을 따로 기록하므로 새 ABI를 옛 레코드로 해석하지 않는다.
        std::memcpy(result.data() + envelope_bytes, &value, sizeof(value));
        finish_envelope(result, record_tag, sequence);
        return result;
    }

    std::array<std::byte, footer_bytes> make_footer(const recording_summary& summary,
        std::uint32_t rolling_crc, bool source_stopped_cleanly)
    {
        std::array<std::byte, footer_bytes> result{};
        const std::uint32_t issues = (summary.issues & persisted_issues) |
            (source_stopped_cleanly ? 0u : caller_incomplete);
        put<std::uint64_t>(result, 32, summary.record_count);
        put<std::uint64_t>(result, 40, summary.valid_bytes);
        put<std::uint32_t>(result, 48, rolling_crc ^ UINT32_MAX);
        put<std::uint32_t>(result, 52, issues);
        put<std::uint64_t>(result, 56, summary.dropped_records);
        put<std::uint64_t>(result, 64, summary.reported_loss_count);
        put<std::uint32_t>(result, 72, source_stopped_cleanly ? 1u : 0u);
        finish_envelope(result, footer_tag, static_cast<std::uint64_t>(summary.record_count) + 1);
        return result;
    }

    void finish_summary(recording_summary& summary, bool source_stopped_cleanly)
    {
        if (!source_stopped_cleanly)
        {
            summary.issues |= caller_incomplete;
        }
        if (!summary.source_stopped)
        {
            summary.issues |= missing_stop;
        }
        summary.complete = summary.finalized && summary.issues == no_issues;
    }

    using execution_key = std::uint64_t;

    execution_key key_of(const record& value)
    {
        // helper 세션에서 유일한 execution_id를 쓴다. begin/end에는 HW queue가
        // 없고 work에만 있으므로 그 값을 키에 넣으면 정상 work가 전부 고아가 된다.
        return value.execution_id;
    }

    bool same_execution_identity(const record& begin, const record& value)
    {
        return begin.process_id == value.process_id && begin.thread_id == value.thread_id &&
            begin.api_queue_id == value.api_queue_id && begin.present_token == value.present_token &&
            begin.cpu_submit_qpc == value.cpu_submit_qpc &&
            begin.source_cpu_submit_ns == value.source_cpu_submit_ns;
    }
}

namespace ce::dx_capture
{
    struct recording_decoder
    {
        static void build_links(recording_snapshot& snapshot)
        {
            using namespace file_detail;
            auto& records = snapshot.records_;
            auto& executions = snapshot.executions_;
            auto& summary = snapshot.summary_;
            executions.reserve(summary.execution_count);
            snapshot.works_.reserve(summary.work_count);
            std::vector<std::pair<execution_key, std::uint32_t>> identities;
            identities.reserve(summary.execution_count);
            std::vector<std::uint32_t> submissions;
            submissions.reserve(summary.submission_count);
            for (std::uint32_t i = 0; i != records.size(); ++i)
            {
                if (records[i].kind == record_kind::gpu_execution_begin)
                {
                    identities.emplace_back(key_of(records[i]), static_cast<std::uint32_t>(executions.size()));
                    executions.push_back({ i });
                    if ((records[i].flags & ambiguous) != 0)
                    {
                        executions.back().association = association_state::ambiguous;
                    }
                }
                else if (records[i].kind == record_kind::submission)
                {
                    submissions.push_back(i);
                }
            }
            std::sort(identities.begin(), identities.end());
            for (std::size_t first = 0; first != identities.size();)
            {
                std::size_t last = first + 1;
                while (last != identities.size() && identities[last].first == identities[first].first)
                {
                    ++last;
                }
                if (last - first != 1)
                {
                    for (auto i = first; i != last; ++i)
                    {
                        executions[identities[i].second].duplicate_execution = true;
                        executions[identities[i].second].association = association_state::ambiguous;
                    }
                    summary.issues |= unfinished_execution;
                }
                first = last;
            }
            const auto find_execution = [&](const record& value) -> std::uint32_t
            {
                const auto key = key_of(value);
                const auto found = std::lower_bound(identities.begin(), identities.end(),
                    std::pair{ key, std::uint32_t{ 0 } });
                if (found == identities.end() || found->first != key ||
                    executions[found->second].duplicate_execution)
                {
                    return no_record;
                }
                return found->second;
            };
            for (std::uint32_t i = 0; i != records.size(); ++i)
            {
                if (records[i].kind != record_kind::gpu_execution_end)
                {
                    continue;
                }
                const auto index = find_execution(records[i]);
                if (index == no_record)
                {
                    summary.issues |= unfinished_execution;
                    continue;
                }
                auto& link = executions[index];
                if ((records[i].flags & ambiguous) != 0)
                {
                    link.association = association_state::ambiguous;
                }
                const auto end_qpc = records[i].qpc_end != 0 ? records[i].qpc_end : records[i].qpc_begin;
                if (link.end_record != no_record || end_qpc < records[link.begin_record].qpc_begin ||
                    !same_execution_identity(records[link.begin_record], records[i]))
                {
                    link.duplicate_execution = true;
                    link.association = association_state::ambiguous;
                    link.end_record = no_record;
                    summary.issues |= unfinished_execution;
                }
                else
                {
                    link.end_record = i;
                }
            }

            // upstream api_queue_id는 합성 ID이고 엔진 submission의 것은 native queue
            // 식별자다. 두 정수가 같아도 같은 큐가 아니다. 같은 프로세스/제출 스레드의
            // CPU 제출 시각을 유일하게 감싼 실제 호출 구간만 귀속 근거로 삼는다.
            std::sort(submissions.begin(), submissions.end(), [&](auto a, auto b)
            {
                return std::tie(records[a].process_id, records[a].thread_id,
                    records[a].qpc_begin, records[a].qpc_end) <
                    std::tie(records[b].process_id, records[b].thread_id,
                        records[b].qpc_begin, records[b].qpc_end);
            });
            std::vector<std::uint32_t> queries;
            queries.reserve(executions.size());
            for (std::uint32_t i = 0; i != executions.size(); ++i)
            {
                const auto& value = records[executions[i].begin_record];
                if (!executions[i].duplicate_execution && (value.flags & has_cpu_submit) != 0 &&
                    value.cpu_submit_qpc != 0 &&
                    executions[i].association != association_state::ambiguous &&
                    value.process_id != 0 && value.thread_id != 0)
                {
                    queries.push_back(i);
                }
            }
            std::sort(queries.begin(), queries.end(), [&](auto a, auto b)
            {
                const auto& left = records[executions[a].begin_record];
                const auto& right = records[executions[b].begin_record];
                return std::tie(left.process_id, left.thread_id, left.cpu_submit_qpc) <
                    std::tie(right.process_id, right.thread_id, right.cpu_submit_qpc);
            });
            std::multiset<std::pair<std::uint64_t, std::uint32_t>> active;
            std::size_t next_submission = 0;
            std::pair<std::uint32_t, std::uint32_t> active_thread{};
            bool have_thread = false;
            for (const auto index : queries)
            {
                auto& link = executions[index];
                const auto& value = records[link.begin_record];
                const auto source_thread = std::pair{ value.process_id, value.thread_id };
                if (!have_thread || active_thread != source_thread)
                {
                    active.clear();
                    active_thread = source_thread;
                    have_thread = true;
                    while (next_submission != submissions.size() &&
                        std::pair{ records[submissions[next_submission]].process_id,
                            records[submissions[next_submission]].thread_id } < active_thread)
                    {
                        ++next_submission;
                    }
                }
                while (next_submission != submissions.size())
                {
                    const auto record_index = submissions[next_submission];
                    const auto& submission = records[record_index];
                    if (std::pair{ submission.process_id, submission.thread_id } != active_thread ||
                        submission.qpc_begin > value.cpu_submit_qpc)
                    {
                        break;
                    }
                    active.emplace(submission.qpc_end, record_index);
                    ++next_submission;
                }
                while (!active.empty() && active.begin()->first < value.cpu_submit_qpc)
                {
                    active.erase(active.begin());
                }
                if (active.size() == 1)
                {
                    const auto submission_index = active.begin()->second;
                    const auto flags = records[submission_index].flags;
                    if ((flags & ambiguous) != 0)
                    {
                        link.association = association_state::ambiguous;
                    }
                    else if ((flags & incomplete) == 0)
                    {
                        link.association = association_state::unique;
                        link.submission_record = submission_index;
                    }
                }
                else if (!active.empty())
                {
                    link.association = association_state::ambiguous;
                }
            }
            for (auto& link : executions)
            {
                if (link.end_record == no_record && !link.duplicate_execution)
                {
                    link.association = association_state::unmatched;
                    link.submission_record = no_record;
                }
                if (link.end_record == no_record)
                {
                    summary.issues |= unfinished_execution;
                }
            }
            for (std::uint32_t i = 0; i != records.size(); ++i)
            {
                if (records[i].kind != record_kind::gpu_work)
                {
                    continue;
                }
                work_link link;
                link.work_record = i;
                link.execution_index = find_execution(records[i]);
                if (link.execution_index != no_record)
                {
                    auto& execution = executions[link.execution_index];
                    if ((records[i].flags & ambiguous) != 0 ||
                        !same_execution_identity(records[execution.begin_record], records[i]))
                    {
                        execution.association = association_state::ambiguous;
                        execution.submission_record = no_record;
                        if (!same_execution_identity(records[execution.begin_record], records[i]))
                        {
                            summary.issues |= unfinished_execution;
                        }
                    }
                    link.submission_record = execution.submission_record;
                    link.association = execution.association;
                    const auto& work = records[i];
                    if (execution.end_record != no_record)
                    {
                        const auto& end = records[execution.end_record];
                        if ((end.qpc_begin != 0 && work.qpc_begin != 0 && work.qpc_begin < end.qpc_begin) ||
                            (end.qpc_end != 0 && work.qpc_end != 0 && work.qpc_end > end.qpc_end) ||
                            (end.source_begin_ns != 0 && work.source_begin_ns != 0 &&
                                work.source_begin_ns < end.source_begin_ns) ||
                            (end.source_end_ns != 0 && work.source_end_ns != 0 &&
                                work.source_end_ns > end.source_end_ns))
                        {
                            summary.issues |= unfinished_execution;
                            execution.association = association_state::unmatched;
                            execution.submission_record = no_record;
                            link.association = association_state::unmatched;
                            link.submission_record = no_record;
                        }
                    }
                }
                else
                {
                    summary.issues |= unfinished_execution;
                    const auto key = key_of(records[i]);
                    const auto found = std::lower_bound(identities.begin(), identities.end(),
                        std::pair{ key, std::uint32_t{ 0 } });
                    if (found != identities.end() && found->first == key)
                    {
                        link.association = association_state::ambiguous;
                    }
                }
                snapshot.works_.push_back(link);
            }
            // 뒤쪽 work에서 실행 경계의 모순이 드러나면 앞쪽 work도 같은 상태를 본다.
            for (auto& link : snapshot.works_)
            {
                if (link.execution_index != no_record)
                {
                    const auto& execution = executions[link.execution_index];
                    link.association = execution.association;
                    link.submission_record = execution.submission_record;
                }
            }
            for (const auto& link : executions)
            {
                summary.unmatched_executions += link.association == association_state::unmatched ? 1u : 0u;
                summary.ambiguous_executions += link.association == association_state::ambiguous ? 1u : 0u;
            }
        }

        static std::expected<recording_snapshot_ptr, file_error>
        decode(std::span<const std::byte> bytes, recording_limits limits)
        {
            using namespace file_detail;
            if (!valid_limits(limits) || bytes.size() > limits.max_file_bytes)
            {
                return std::unexpected(file_error::resource_limit);
            }
            if (bytes.size() < header_bytes)
            {
                return std::unexpected(file_error::truncated);
            }
            if (std::memcmp(bytes.data(), magic.data(), magic.size()) != 0)
            {
                return std::unexpected(file_error::not_a_capture);
            }
            if (get<std::uint32_t>(bytes, 8) != capture_file_version ||
                get<std::uint32_t>(bytes, 44) != protocol_version)
            {
                return std::unexpected(file_error::unsupported_version);
            }
            if (get<std::uint32_t>(bytes, 12) != header_bytes ||
                get<std::uint32_t>(bytes, 48) != sizeof(record) || get<std::uint64_t>(bytes, 52) != 0)
            {
                return std::unexpected(file_error::malformed);
            }
            if (get<std::uint32_t>(bytes, 60) != crc32(bytes.first(60)))
            {
                return std::unexpected(file_error::checksum_mismatch);
            }
            auto snapshot = std::make_shared<recording_snapshot>();
            auto& summary = snapshot->summary_;
            summary.file_bytes = bytes.size();
            summary.valid_bytes = header_bytes;
            snapshot->records_.reserve((std::min)(static_cast<std::size_t>(limits.max_records),
                (bytes.size() - header_bytes) / record_bytes));
            std::size_t offset = header_bytes;
            auto rolling_crc = update_crc(UINT32_MAX, bytes.first(header_bytes));
            bool claimed_complete = false;
            while (offset != bytes.size())
            {
                if (bytes.size() - offset < envelope_bytes)
                {
                    summary.issues |= truncated_record;
                    break;
                }
                const auto envelope = bytes.subspan(offset, envelope_bytes);
                const auto tag = get<std::uint32_t>(envelope, 0);
                const auto payload_size = get<std::uint32_t>(envelope, 16);
                if (get<std::uint32_t>(envelope, 4) != envelope_version)
                {
                    return std::unexpected(file_error::unsupported_version);
                }
                if ((tag != record_tag && tag != footer_tag) ||
                    payload_size != (tag == record_tag ? sizeof(record) : footer_payload_bytes) ||
                    get<std::uint64_t>(envelope, 24) != 0)
                {
                    return std::unexpected(file_error::malformed);
                }
                if (get<std::uint64_t>(envelope, 8) != static_cast<std::uint64_t>(summary.record_count) + 1)
                {
                    return std::unexpected(file_error::invalid_sequence);
                }
                const std::size_t total_size = envelope_bytes + payload_size;
                if (bytes.size() - offset < total_size)
                {
                    summary.issues |= truncated_record;
                    break;
                }
                const auto payload = bytes.subspan(offset + envelope_bytes, payload_size);
                const auto checksum = update_crc(update_crc(UINT32_MAX, envelope.first(20)), payload) ^ UINT32_MAX;
                if (get<std::uint32_t>(envelope, 20) != checksum)
                {
                    return std::unexpected(file_error::checksum_mismatch);
                }
                if (tag == footer_tag)
                {
                    const auto issues = get<std::uint32_t>(payload, 20);
                    const auto complete_value = get<std::uint32_t>(payload, 40);
                    const auto drops = get<std::uint64_t>(payload, 24);
                    if (get<std::uint32_t>(payload, 16) != (rolling_crc ^ UINT32_MAX))
                    {
                        return std::unexpected(file_error::checksum_mismatch);
                    }
                    if (get<std::uint64_t>(payload, 0) != summary.record_count ||
                        get<std::uint64_t>(payload, 8) != offset ||
                        (issues & ~persisted_issues) != 0 || complete_value > 1 ||
                        get<std::uint32_t>(payload, 44) != 0 ||
                        get<std::uint64_t>(payload, 32) != summary.reported_loss_count ||
                        (issues & summary.issues) != summary.issues ||
                        (drops != 0 && (issues & (record_limit | writer_error)) == 0) ||
                        (complete_value == 0 && (issues & caller_incomplete) == 0) ||
                        offset + total_size != bytes.size())
                    {
                        return std::unexpected(file_error::malformed);
                    }
                    summary.dropped_records = drops;
                    summary.issues |= issues;
                    summary.finalized = true;
                    summary.valid_bytes += total_size;
                    claimed_complete = complete_value != 0;
                    offset += total_size;
                    break;
                }
                if (summary.record_count >= limits.max_records)
                {
                    return std::unexpected(file_error::resource_limit);
                }
                record value{};
                std::memcpy(&value, payload.data(), sizeof(value));
                const auto validation = validate_record(value,
                    snapshot->records_.empty() ? nullptr : &snapshot->records_.front());
                if (!validation)
                {
                    return std::unexpected(validation.error());
                }
                if (snapshot->records_.empty() && (value.qpc_frequency != get<std::uint64_t>(bytes, 16) ||
                    value.qpc_begin != get<std::uint64_t>(bytes, 24) ||
                    value.process_creation_time != get<std::uint64_t>(bytes, 32) ||
                    value.process_id != get<std::uint32_t>(bytes, 40)))
                {
                    return std::unexpected(file_error::malformed);
                }
                snapshot->records_.push_back(value);
                observe(summary, value);
                rolling_crc = update_crc(rolling_crc, bytes.subspan(offset, total_size));
                offset += total_size;
                summary.valid_bytes = offset;
            }
            if (snapshot->records_.empty())
            {
                return std::unexpected(file_error::truncated);
            }
            if (!summary.finalized)
            {
                summary.issues |= missing_footer;
            }
            build_links(*snapshot);
            finish_summary(summary, claimed_complete);
            return recording_snapshot_ptr(std::move(snapshot));
        }
    };

    const char* describe(file_error error)
    {
        switch (error)
        {
        case file_error::open_failed: return "Cannot open the DX capture file";
        case file_error::read_failed: return "Cannot read the DX capture file";
        case file_error::write_failed: return "Cannot write the DX capture file";
        case file_error::not_a_capture: return "This is not a .cedx raw capture";
        case file_error::unsupported_version: return "Unsupported DX capture or protocol version";
        case file_error::truncated: return "The DX capture has no complete session record";
        case file_error::checksum_mismatch: return "DX capture integrity check failed";
        case file_error::resource_limit: return "DX capture exceeds the bounded reader or writer limit";
        case file_error::malformed: return "Malformed DX capture record";
        case file_error::invalid_sequence: return "DX capture record sequence is not contiguous";
        case file_error::invalid_qpc: return "Invalid raw QPC metadata or interval";
        }
        return "Unknown DX capture error";
    }

    std::expected<recording_snapshot_ptr, file_error>
    decode_recording(std::span<const std::byte> bytes, recording_limits limits)
    {
        try
        {
            return recording_decoder::decode(bytes, limits);
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(file_error::resource_limit);
        }
    }

    std::expected<recording_snapshot_ptr, file_error>
    read_recording(const std::filesystem::path& path, recording_limits limits)
    {
        if (!file_detail::valid_limits(limits))
        {
            return std::unexpected(file_error::resource_limit);
        }
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return std::unexpected(file_error::open_failed);
        }
        const auto size = stream.tellg();
        if (size < 0)
        {
            return std::unexpected(file_error::read_failed);
        }
        if (static_cast<std::uint64_t>(size) > limits.max_file_bytes)
        {
            return std::unexpected(file_error::resource_limit);
        }
        try
        {
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            stream.seekg(0);
            if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
            {
                return std::unexpected(file_error::read_failed);
            }
            // 읽기 도중 붙은 꼬리도 완료된 스냅샷으로 오인하지 않는다.
            if (stream.peek() != std::char_traits<char>::eof())
            {
                return std::unexpected(file_error::read_failed);
            }
            return decode_recording(bytes, limits);
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(file_error::resource_limit);
        }
    }

    std::expected<std::vector<std::byte>, file_error>
    encode_recording(std::span<const record> records, bool source_stopped_cleanly, recording_limits limits)
    {
        using namespace file_detail;
        if (!valid_limits(limits) || records.size() > limits.max_records ||
            records.size() > (limits.max_file_bytes - header_bytes - footer_bytes) / record_bytes)
        {
            return std::unexpected(file_error::resource_limit);
        }
        if (records.empty())
        {
            return std::unexpected(file_error::malformed);
        }
        try
        {
            std::vector<std::byte> bytes;
            bytes.reserve(header_bytes + records.size() * record_bytes + footer_bytes);
            const auto header = make_header(records.front());
            bytes.insert(bytes.end(), header.begin(), header.end());
            recording_summary summary;
            summary.valid_bytes = header_bytes;
            auto rolling_crc = update_crc(UINT32_MAX, header);
            for (std::size_t i = 0; i != records.size(); ++i)
            {
                const auto validation = validate_record(records[i], i == 0 ? nullptr : &records.front());
                if (!validation)
                {
                    return std::unexpected(validation.error());
                }
                const auto encoded = make_record(records[i], i + 1);
                bytes.insert(bytes.end(), encoded.begin(), encoded.end());
                rolling_crc = update_crc(rolling_crc, encoded);
                observe(summary, records[i]);
                summary.valid_bytes += encoded.size();
            }
            const auto footer = make_footer(summary, rolling_crc, source_stopped_cleanly);
            bytes.insert(bytes.end(), footer.begin(), footer.end());
            return bytes;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(file_error::resource_limit);
        }
    }

    struct recording_writer::implementation
    {
        std::ofstream stream;
        record session;
        recording_limits limits;
        recording_summary summary;
        std::uint32_t rolling_crc = UINT32_MAX;
        bool failed = false;
        struct execution_bounds
        {
            std::uint64_t qpc_begin = 0;
            std::uint64_t qpc_end = 0;
            std::uint64_t source_begin_ns = 0;
            std::uint64_t source_end_ns = 0;
        };
        std::map<file_detail::execution_key, execution_bounds> open_executions;

        bool write(std::span<const std::byte> bytes)
        {
            if (!stream.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
            {
                failed = true;
                summary.issues |= writer_error;
                return false;
            }
            summary.valid_bytes += bytes.size();
            summary.file_bytes = summary.valid_bytes;
            return true;
        }
    };

    recording_writer::recording_writer(std::unique_ptr<implementation> implementation)
        : implementation_(std::move(implementation))
    {
    }

    std::expected<std::unique_ptr<recording_writer>, file_error>
    recording_writer::open(const std::filesystem::path& path, const record& session, recording_limits limits)
    {
        using namespace file_detail;
        if (!valid_limits(limits))
        {
            return std::unexpected(file_error::resource_limit);
        }
        const auto validation = validate_record(session, nullptr);
        if (!validation)
        {
            return std::unexpected(validation.error());
        }
        auto implementation = std::make_unique<recording_writer::implementation>();
        implementation->session = session;
        implementation->limits = limits;
        implementation->stream.open(path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (!implementation->stream)
        {
            return std::unexpected(file_error::open_failed);
        }
        const auto header = make_header(session);
        const auto first = make_record(session, 1);
        if (!implementation->write(header) || !implementation->write(first))
        {
            return std::unexpected(file_error::write_failed);
        }
        implementation->rolling_crc = update_crc(update_crc(UINT32_MAX, header), first);
        observe(implementation->summary, session);
        return std::unique_ptr<recording_writer>(new recording_writer(std::move(implementation)));
    }

    recording_writer::~recording_writer()
    {
        if (implementation_ && !implementation_->summary.finalized && !implementation_->failed)
        {
            // 명시적 정상 종료 없이 소멸하면 읽을 수는 있어도 완전한 캡처는 아니다.
            finalize(false);
        }
    }

    bool recording_writer::append(const record& value)
    {
        using namespace file_detail;
        auto& state = *implementation_;
        if (state.summary.finalized || state.failed)
        {
            return false;
        }
        if (!validate_record(value, &state.session))
        {
            state.summary.issues |= writer_error;
            if (state.summary.dropped_records != UINT64_MAX)
            {
                ++state.summary.dropped_records;
            }
            return false;
        }
        if (state.summary.record_count >= state.limits.max_records ||
            state.summary.valid_bytes + record_bytes + footer_bytes > state.limits.max_file_bytes)
        {
            state.summary.issues |= record_limit;
            if (state.summary.dropped_records != UINT64_MAX)
            {
                ++state.summary.dropped_records;
            }
            return false;
        }
        const auto encoded = make_record(value, static_cast<std::uint64_t>(state.summary.record_count) + 1);
        if (!state.write(encoded))
        {
            return false;
        }
        state.rolling_crc = update_crc(state.rolling_crc, encoded);
        observe(state.summary, value);
        if (value.kind == record_kind::gpu_execution_begin)
        {
            if (!state.open_executions.emplace(key_of(value), implementation::execution_bounds{}).second)
            {
                state.summary.issues |= unfinished_execution;
            }
        }
        else if (value.kind == record_kind::gpu_execution_end)
        {
            const auto found = state.open_executions.find(key_of(value));
            if (found == state.open_executions.end())
            {
                state.summary.issues |= unfinished_execution;
            }
            else
            {
                const auto& bounds = found->second;
                if ((value.qpc_begin != 0 && bounds.qpc_begin != 0 && bounds.qpc_begin < value.qpc_begin) ||
                    (value.qpc_end != 0 && bounds.qpc_end > value.qpc_end) ||
                    (value.source_begin_ns != 0 && bounds.source_begin_ns != 0 &&
                        bounds.source_begin_ns < value.source_begin_ns) ||
                    (value.source_end_ns != 0 && bounds.source_end_ns > value.source_end_ns))
                {
                    state.summary.issues |= unfinished_execution;
                }
                state.open_executions.erase(found);
            }
        }
        else if (value.kind == record_kind::gpu_work)
        {
            const auto found = state.open_executions.find(key_of(value));
            if (found == state.open_executions.end())
            {
                state.summary.issues |= unfinished_execution;
            }
            else
            {
                auto& bounds = found->second;
                if (value.qpc_begin != 0)
                {
                    bounds.qpc_begin = bounds.qpc_begin == 0 ? value.qpc_begin :
                        (std::min)(bounds.qpc_begin, value.qpc_begin);
                }
                bounds.qpc_end = (std::max)(bounds.qpc_end, value.qpc_end);
                if (value.source_begin_ns != 0)
                {
                    bounds.source_begin_ns = bounds.source_begin_ns == 0 ? value.source_begin_ns :
                        (std::min)(bounds.source_begin_ns, value.source_begin_ns);
                }
                bounds.source_end_ns = (std::max)(bounds.source_end_ns, value.source_end_ns);
            }
        }
        return true;
    }

    std::expected<void, file_error> recording_writer::finalize(bool source_stopped_cleanly)
    {
        using namespace file_detail;
        auto& state = *implementation_;
        if (state.failed)
        {
            return std::unexpected(file_error::write_failed);
        }
        if (state.summary.finalized)
        {
            return {};
        }
        if (!state.open_executions.empty())
        {
            state.summary.issues |= unfinished_execution;
        }
        const auto footer = make_footer(state.summary, state.rolling_crc, source_stopped_cleanly);
        if (!state.write(footer))
        {
            return std::unexpected(file_error::write_failed);
        }
        state.stream.flush();
        if (!state.stream)
        {
            state.failed = true;
            state.summary.issues |= writer_error;
            return std::unexpected(file_error::write_failed);
        }
        state.stream.close();
        if (state.stream.fail())
        {
            state.failed = true;
            state.summary.issues |= writer_error;
            return std::unexpected(file_error::write_failed);
        }
        state.summary.finalized = true;
        finish_summary(state.summary, source_stopped_cleanly);
        return {};
    }

    const recording_summary& recording_writer::status() const
    {
        return implementation_->summary;
    }
}
