// 소스 fixture만 추가한다. 이번 변경에서는 빌드하거나 실행하지 않았다.
#include "DxCaptureFile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace dx_capture_file_tests
{
    using namespace ce::dx_capture;
    constexpr std::size_t header_bytes = 64;
    constexpr std::size_t envelope_bytes = 32;
    constexpr std::size_t wire_record_bytes = envelope_bytes + sizeof(record);
    constexpr std::size_t footer_bytes = 80;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "DxCaptureFileTests: %s\n", message);
            std::abort();
        }
    }

    template<class T>
    void put(std::vector<std::byte>& bytes, std::size_t offset, T value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
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

    void repair_header(std::vector<std::byte>& bytes)
    {
        put(bytes, 60, update_crc(UINT32_MAX, std::span(bytes).first(60)) ^ UINT32_MAX);
    }

    void repair_envelope(std::vector<std::byte>& bytes, std::size_t offset, std::size_t payload_bytes)
    {
        const auto value = update_crc(update_crc(UINT32_MAX, std::span(bytes).subspan(offset, 20)),
            std::span(bytes).subspan(offset + envelope_bytes, payload_bytes)) ^ UINT32_MAX;
        put(bytes, offset + 20, value);
    }

    void repair_footer(std::vector<std::byte>& bytes)
    {
        const auto offset = bytes.size() - footer_bytes;
        put(bytes, offset + 48, update_crc(UINT32_MAX, std::span(bytes).first(offset)) ^ UINT32_MAX);
        repair_envelope(bytes, offset, footer_bytes - envelope_bytes);
    }

    std::vector<record> fixture()
    {
        record session;
        session.kind = record_kind::session;
        session.qpc_begin = 1000;
        session.qpc_frequency = 10000000;
        session.process_id = 123;
        session.process_creation_time = 456789;
        session.windows_session_id = 2;
        session.flags = raw_api_marker_ids | pix_markers_unavailable;

        record execution;
        execution.kind = record_kind::gpu_execution_begin;
        execution.flags = has_cpu_submit;
        execution.qpc_begin = 0;
        execution.cpu_submit_qpc = 1110;
        execution.source_begin_ns = 0;
        execution.source_cpu_submit_ns = 111000;
        execution.process_id = session.process_id;
        execution.thread_id = 42;
        execution.api_queue_id = 3;
        execution.hardware_queue_id = 0;
        execution.execution_id = 98;
        execution.command_list_count = 0;
        execution.present_token = UINT64_C(0xE123456789ABCDEF);

        record work = execution;
        work.kind = record_kind::gpu_work;
        work.flags |= calibrated_qpc;
        work.hardware_queue_id = 17;
        work.qpc_begin = 2100;
        work.qpc_end = 2300;
        work.source_begin_ns = 210000;
        work.source_end_ns = 230000;
        work.api_marker_id = UINT64_C(0xF123456789ABCDEF);
        work.command_list_index = 19347;

        record end = execution;
        end.kind = record_kind::gpu_execution_end;
        end.flags |= calibrated_qpc;
        end.qpc_begin = 2100;
        end.qpc_end = 2300;
        end.source_begin_ns = 210000;
        end.source_end_ns = 230000;

        record stopped;
        stopped.kind = record_kind::status;
        stopped.status = status_code::stopped;
        stopped.process_id = session.process_id;
        stopped.qpc_begin = 2500;

        record submission;
        submission.kind = record_kind::submission;
        submission.process_id = session.process_id;
        submission.thread_id = execution.thread_id;
        submission.api_queue_id = UINT64_C(0xABCDEF0000);
        submission.submission_id = 33;
        submission.engine_frame_id = 200;
        submission.render_view_id = 4;
        submission.qpc_begin = 1100;
        submission.qpc_end = 1120;

        // ETW 전송 뒤 도착한 엔진 metadata도 원시 순서 그대로 보존하고 나중에 귀속한다.
        return { session, execution, work, end, stopped, submission };
    }

    recording_snapshot_ptr round_trip(const std::vector<record>& records, bool complete = true)
    {
        const auto encoded = encode_recording(records, complete);
        check(encoded.has_value(), "fixture must encode");
        const auto decoded = decode_recording(*encoded);
        check(decoded.has_value(), "fixture must decode");
        return *decoded;
    }

    void raw_round_trip_and_mapping()
    {
        const auto records = fixture();
        const auto decoded = round_trip(records);
        check(decoded->summary().finalized && decoded->summary().complete, "clean stopped capture is complete");
        check(decoded->summary().first_qpc == 1000 && decoded->summary().last_qpc == 2500,
            "QPC overview covers original raw intervals");
        check(decoded->records().size() == records.size(), "all raw records survive");
        for (std::size_t i = 0; i != records.size(); ++i)
        {
            check(std::memcmp(&records[i], &decoded->records()[i], sizeof(record)) == 0,
                "all upstream identities, source ns, and QPC values survive exactly");
        }
        check(decoded->execution_links().size() == 1 && decoded->work_links().size() == 1,
            "execution and work indexes exist");
        const auto& link = decoded->execution_links().front();
        check(link.association == association_state::unique && link.submission_record == 5,
            "exact same-process/thread enclosing submission matches despite different queue integers");
        check(link.begin_record == 1 && link.end_record == 3, "execution retains original record indexes");
        check(decoded->work_links().front().execution_index == 0 &&
            decoded->work_links().front().submission_record == 5, "work inherits only proven association");
    }

    void ambiguity_and_native_queue_traps()
    {
        auto records = fixture();
        record wrong_thread = records.back();
        wrong_thread.api_queue_id = records[1].api_queue_id;
        wrong_thread.thread_id = 43;
        wrong_thread.submission_id = 900;
        records.push_back(wrong_thread);
        auto decoded = round_trip(records);
        check(decoded->execution_links().front().submission_record == 5,
            "equal numeric synthetic/native queue ID cannot override the submitting thread");

        records.erase(records.begin() + 5);
        decoded = round_trip(records);
        check(decoded->execution_links().front().association == association_state::unmatched,
            "synthetic/native numeric queue equality alone never matches");

        records = fixture();
        record overlapping = records.back();
        overlapping.api_queue_id = records[1].api_queue_id;
        overlapping.submission_id = 34;
        records.push_back(overlapping);
        decoded = round_trip(records);
        check(decoded->execution_links().front().association == association_state::ambiguous &&
            decoded->execution_links().front().submission_record == no_record,
            "all enclosing same-thread intervals count, regardless of queue integer equality");
        check(decoded->summary().ambiguous_executions == 1, "ambiguity remains visible in summary");

        records = fixture();
        for (std::size_t i = 1; i <= 3; ++i)
        {
            records[i].cpu_submit_qpc = 1099;
            records[i].source_cpu_submit_ns = 109900;
        }
        decoded = round_trip(records);
        check(decoded->execution_links().front().association == association_state::unmatched,
            "nearest frame or nearest interval is never guessed");

        records = fixture();
        records.back().qpc_end = 1110;
        record adjacent = records.back();
        adjacent.qpc_begin = 1110;
        adjacent.qpc_end = 1130;
        records.push_back(adjacent);
        decoded = round_trip(records);
        check(decoded->execution_links().front().association == association_state::ambiguous,
            "shared inclusive interval endpoint is ambiguous");

        records = fixture();
        records[1].flags &= ~has_cpu_submit;
        decoded = round_trip(records);
        check(decoded->execution_links().front().association == association_state::unmatched,
            "a timestamp without submission evidence cannot be used for correlation");

        records = fixture();
        records[1].flags |= ambiguous;
        decoded = round_trip(records);
        check(decoded->execution_links().front().association == association_state::ambiguous &&
            decoded->work_links().front().submission_record == no_record,
            "explicit source ambiguity is never upgraded to unique by a matching interval");
    }

    void incomplete_and_loss_are_visible()
    {
        auto records = fixture();
        record loss;
        loss.kind = record_kind::status;
        loss.status = status_code::events_lost;
        loss.flags = etw_loss | transport_loss;
        loss.loss_count = 7;
        loss.etw_events_lost = 5;
        loss.etw_buffers_lost = 2;
        records.insert(records.begin() + 4, loss);
        auto decoded = round_trip(records);
        check(decoded->summary().finalized && !decoded->summary().complete,
            "a valid footer cannot hide known source loss");
        check((decoded->summary().issues & source_loss) != 0 && decoded->summary().reported_loss_count == 7,
            "loss status with zero process/QPC is retained");
        check(decoded->summary().etw_events_lost == 5 && decoded->summary().etw_buffers_lost == 2,
            "lost ETW events and buffers are not added as interchangeable units");

        records = fixture();
        records.erase(records.begin() + 4);
        decoded = round_trip(records);
        check(!decoded->summary().complete && (decoded->summary().issues & missing_stop) != 0,
            "caller complete does not substitute for helper stopped evidence");

        records = fixture();
        records.erase(records.begin() + 3);
        decoded = round_trip(records);
        check(!decoded->summary().complete && (decoded->summary().issues & unfinished_execution) != 0,
            "missing execution end remains incomplete");
        check(decoded->work_links().front().association == association_state::unmatched &&
            decoded->work_links().front().submission_record == no_record,
            "missing end does not silently give work a completed execution identity");

        records = fixture();
        records[3].qpc_begin = 2200;
        records[3].source_begin_ns = 220000;
        decoded = round_trip(records);
        check(!decoded->summary().complete && (decoded->summary().issues & unfinished_execution) != 0 &&
            decoded->work_links().front().association == association_state::unmatched,
            "work outside declared execution bounds is visible as incomplete and unassociated");

        records = fixture();
        records.erase(records.begin() + 2);
        records[2].flags |= incomplete;
        records[2].qpc_begin = records[2].qpc_end = 0;
        records[2].source_begin_ns = records[2].source_end_ns = 0;
        decoded = round_trip(records);
        check(!decoded->summary().complete && decoded->work_links().empty(),
            "execution without any GPU work keeps zero GPU time and explicit incomplete end");

        records = fixture();
        records.insert(records.begin() + 2, records[1]);
        decoded = round_trip(records);
        check(!decoded->summary().complete && decoded->work_links().front().association == association_state::ambiguous,
            "duplicate execution identity does not silently select a begin");

        records = fixture();
        records.insert(records.begin() + 4, records[3]);
        decoded = round_trip(records);
        check(!decoded->summary().complete && decoded->work_links().front().association == association_state::ambiguous &&
            decoded->work_links().front().submission_record == no_record,
            "duplicate execution end cannot supply a unique work identity");

        records = fixture();
        records[2].api_queue_id = 999;
        decoded = round_trip(records);
        check(!decoded->summary().complete && decoded->work_links().front().association == association_state::ambiguous,
            "matching execution ID with contradictory original queue identity is not uniquely associated");

        records = fixture();
        decoded = round_trip(records, false);
        check(!decoded->summary().complete && (decoded->summary().issues & caller_incomplete) != 0,
            "abandoned caller remains incomplete even with stopped record");
    }

    void truncation_and_integrity()
    {
        const auto encoded = encode_recording(fixture(), true);
        check(encoded.has_value(), "truncation fixture encodes");
        for (std::size_t size = 0; size != encoded->size(); ++size)
        {
            const auto decoded = decode_recording(std::span(*encoded).first(size));
            if (size < header_bytes + wire_record_bytes)
            {
                check(!decoded && decoded.error() == file_error::truncated, "no partial session can be read");
            }
            else
            {
                check(decoded && !(*decoded)->summary().finalized && !(*decoded)->summary().complete,
                    "every incomplete record/footer prefix remains incomplete");
                check(((*decoded)->summary().issues & missing_footer) != 0,
                    "missing footer is explicit even at a record boundary");
                check((*decoded)->summary().valid_bytes <= size, "recovery never claims unvalidated tail bytes");
            }
        }

        auto corrupt = *encoded;
        corrupt[24] ^= std::byte{ 1 };
        auto decoded = decode_recording(corrupt);
        check(!decoded && decoded.error() == file_error::checksum_mismatch, "header CRC detects metadata edits");
        corrupt = *encoded;
        corrupt[header_bytes + envelope_bytes + 72] ^= std::byte{ 1 };
        decoded = decode_recording(corrupt);
        check(!decoded && decoded.error() == file_error::checksum_mismatch, "record CRC detects ID edits");
        corrupt = *encoded;
        const auto work_offset = header_bytes + 2 * wire_record_bytes;
        corrupt[work_offset + envelope_bytes + offsetof(record, api_marker_id)] ^= std::byte{ 1 };
        repair_envelope(corrupt, work_offset, sizeof(record));
        decoded = decode_recording(corrupt);
        check(!decoded && decoded.error() == file_error::checksum_mismatch,
            "footer whole-prefix CRC detects a changed record with repaired local CRC");
        corrupt = *encoded;
        corrupt.back() ^= std::byte{ 1 };
        decoded = decode_recording(corrupt);
        check(!decoded && decoded.error() == file_error::checksum_mismatch, "footer CRC detects footer edits");
        corrupt = *encoded;
        corrupt.push_back(std::byte{});
        decoded = decode_recording(corrupt);
        check(!decoded && decoded.error() == file_error::malformed, "trailing bytes after footer are not hidden");
    }

    void size_version_sequence_and_qpc_validation()
    {
        const auto baseline = encode_recording(fixture(), true);
        check(baseline.has_value(), "validation fixture encodes");
        auto bytes = *baseline;
        put<std::uint32_t>(bytes, 8, capture_file_version + 1);
        repair_header(bytes);
        auto decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::unsupported_version, "unknown file version rejected");
        bytes = *baseline;
        put<std::uint32_t>(bytes, 44, protocol_version + 1);
        repair_header(bytes);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::unsupported_version, "unknown record ABI rejected");

        bytes = *baseline;
        put<std::uint32_t>(bytes, header_bytes + 16, UINT32_MAX);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::malformed, "oversized payload is rejected before allocation");
        recording_limits limits;
        limits.max_file_bytes = baseline->size() - 1;
        decoded = decode_recording(*baseline, limits);
        check(!decoded && decoded.error() == file_error::resource_limit, "file cap is enforced before decoding");
        limits = {};
        limits.max_records = 2;
        decoded = decode_recording(*baseline, limits);
        check(!decoded && decoded.error() == file_error::resource_limit, "record count cap is enforced");
        check(!encode_recording(fixture(), true, limits), "encoder obeys the same bounded count");
        limits.max_records = maximum_file_records + 1;
        check(!decode_recording(*baseline, limits), "caller cannot raise absolute allocation cap");

        bytes = *baseline;
        put<std::uint64_t>(bytes, header_bytes + wire_record_bytes + 8, 1);
        repair_envelope(bytes, header_bytes + wire_record_bytes, sizeof(record));
        repair_footer(bytes);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::invalid_sequence, "duplicate sequence rejected with valid CRCs");
        bytes = *baseline;
        put<std::uint64_t>(bytes, header_bytes + 8, 2);
        repair_envelope(bytes, header_bytes, sizeof(record));
        repair_footer(bytes);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::invalid_sequence, "sequence gap rejected with valid CRCs");

        auto records = fixture();
        records.front().qpc_frequency = 0;
        const auto no_frequency = encode_recording(records, true);
        check(!no_frequency && no_frequency.error() == file_error::invalid_qpc, "zero QPC frequency rejected");
        records = fixture();
        records.back().qpc_end = records.back().qpc_begin - 1;
        const auto backwards = encode_recording(records, true);
        check(!backwards && backwards.error() == file_error::invalid_qpc, "backwards submit interval rejected");
        bytes = *baseline;
        const auto submission_offset = header_bytes + 5 * wire_record_bytes;
        put<std::uint64_t>(bytes, submission_offset + envelope_bytes + offsetof(record, qpc_end), 1099);
        repair_envelope(bytes, submission_offset, sizeof(record));
        repair_footer(bytes);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::invalid_qpc, "reader validates QPC even with valid CRCs");

        bytes = *baseline;
        const auto work_offset = header_bytes + 2 * wire_record_bytes;
        put<std::uint64_t>(bytes, work_offset + envelope_bytes + offsetof(record, qpc_end), 0);
        repair_envelope(bytes, work_offset, sizeof(record));
        repair_footer(bytes);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::invalid_qpc,
            "nonzero work begin with zero end cannot reach unsigned duration arithmetic");
        bytes = *baseline;
        put<std::uint64_t>(bytes, work_offset + envelope_bytes + offsetof(record, source_end_ns), 1);
        repair_envelope(bytes, work_offset, sizeof(record));
        repair_footer(bytes);
        decoded = decode_recording(bytes);
        check(!decoded && decoded.error() == file_error::invalid_qpc,
            "reversed raw source nanoseconds are rejected even when calibrated QPC looks valid");

        records = fixture();
        for (std::size_t i = 1; i <= 3; ++i)
        {
            records[i].flags &= ~(calibrated_qpc | has_cpu_submit);
            records[i].flags |= incomplete;
            records[i].qpc_begin = 0;
            records[i].qpc_end = 0;
            records[i].cpu_submit_qpc = 0;
        }
        const auto raw_only = round_trip(records);
        check(raw_only->records()[2].source_begin_ns == 210000 &&
            raw_only->execution_links().front().association == association_state::unmatched,
            "uncalibrated source ns survives without invented QPC correlation");
    }
}

int main()
{
    using namespace dx_capture_file_tests;
    raw_round_trip_and_mapping();
    ambiguity_and_native_queue_traps();
    incomplete_and_loss_are_visible();
    truncation_and_integrity();
    size_version_sequence_and_qpc_validation();
    return 0;
}
