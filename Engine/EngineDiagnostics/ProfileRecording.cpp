#include "ProfileCaptureFile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// 유니티 빌드에서 다른 파일의 내부 함수와 충돌하지 않도록 이름 있는 네임스페이스를 쓴다.
namespace ce::detail::recording_file_impl
{
    constexpr std::array<char, 8> kMagic{'C', 'E', 'P', 'R', 'O', 'F', '\0', '\0'};
    constexpr std::uint32_t kRecordMagic = 0x4B435043; // CPCK, little-endian
    constexpr std::size_t kHeaderBytes = 16;
    constexpr std::size_t kEnvelopeBytes = 32;
    constexpr std::size_t kMaximumRecordBytes = 32ull * 1024ull * 1024ull;
    constexpr std::size_t kMaximumMetadataBytes = 16ull * 1024ull * 1024ull;
    constexpr std::size_t kMaximumBins = 2048;
    constexpr std::size_t kEventBytes = 62;
    constexpr std::size_t kCounterBytes = 34;
    constexpr std::size_t kFrameBytes = 36;
    constexpr std::uint32_t kMetadataRecord = 1;
    constexpr std::uint32_t kFrameRecord = 2;
    constexpr std::uint32_t kFinalizeRecord = 3;
    static_assert(std::endian::native == std::endian::little);

    std::uint64_t add(std::uint64_t left, std::uint64_t right)
    {
        return right > (std::numeric_limits<std::uint64_t>::max)() - left
            ? (std::numeric_limits<std::uint64_t>::max)() : left + right;
    }

    std::uint32_t update_crc(std::uint32_t value, std::span<const std::byte> bytes)
    {
        // 생산자 경로 밖에서 한 번만 만든다.
        static const auto table = []
        {
            std::array<std::uint32_t, 256> result{};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t entry = i;
                for (int bit = 0; bit < 8; ++bit)
                {
                    entry = (entry & 1) != 0 ? 0xEDB88320u ^ (entry >> 1) : entry >> 1;
                }
                result[i] = entry;
            }
            return result;
        }();
        for (std::byte byte : bytes)
        {
            value = table[(value ^ std::to_integer<std::uint8_t>(byte)) & 0xFFu] ^ (value >> 8);
        }
        return value;
    }

    std::uint32_t crc(std::span<const std::byte> bytes)
    {
        return update_crc(0xFFFFFFFFu, bytes) ^ 0xFFFFFFFFu;
    }

    struct bytes_writer
    {
        std::vector<std::byte> bytes;
        template<typename T>
        void put(T value)
        {
            static_assert(std::is_integral_v<T>);
            const std::size_t offset = bytes.size();
            bytes.resize(offset + sizeof(T));
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        }
    };

    struct bytes_reader
    {
        std::span<const std::byte> bytes;
        std::size_t offset = 0;
        template<typename T>
        bool get(T& value)
        {
            if (sizeof(T) > bytes.size() - offset)
            {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }
        std::size_t remaining() const { return bytes.size() - offset; }
    };

    bool read_at(std::ifstream& input, std::uint64_t offset, std::span<std::byte> bytes)
    {
        if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) ||
            bytes.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
        {
            return false;
        }
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset));
        return bytes.empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()),
                                                            static_cast<std::streamsize>(bytes.size())));
    }

    bool write_bytes(std::ofstream& output, std::span<const std::byte> bytes)
    {
        return static_cast<bool>(output.write(reinterpret_cast<const char*>(bytes.data()),
                                              static_cast<std::streamsize>(bytes.size())));
    }

    std::vector<std::byte> file_header()
    {
        bytes_writer result;
        for (char value : kMagic)
        {
            result.put(static_cast<std::uint8_t>(value));
        }
        result.put(kRecordingFileVersion);
        result.put(std::uint32_t{0});
        return std::move(result.bytes);
    }

    bool write_record(std::ofstream& output, std::uint32_t type, std::uint64_t sequence,
                      std::span<const std::byte> payload, std::uint64_t& written)
    {
        if (payload.size() > kMaximumRecordBytes ||
            written > (std::numeric_limits<std::uint64_t>::max)() - kEnvelopeBytes - payload.size())
        {
            return false;
        }
        bytes_writer header;
        header.put(kRecordMagic);
        header.put(type);
        header.put(sequence);
        header.put(static_cast<std::uint64_t>(payload.size()));
        header.put(crc(payload));
        header.put(crc(header.bytes));
        if (!write_bytes(output, header.bytes) || !write_bytes(output, payload))
        {
            return false;
        }
        written += kEnvelopeBytes + payload.size();
        return true;
    }

    struct disk_record
    {
        std::uint32_t type = 0;
        std::uint64_t sequence = 0;
        std::uint64_t next = 0;
        std::vector<std::byte> payload;
    };

    std::expected<disk_record, capture_file_error> read_record(std::ifstream& input, std::uint64_t offset,
                                                             std::uint64_t file_size)
    {
        if (offset > file_size || file_size - offset < kEnvelopeBytes)
        {
            return std::unexpected(capture_file_error::truncated);
        }
        std::array<std::byte, kEnvelopeBytes> header{};
        if (!read_at(input, offset, header))
        {
            return std::unexpected(capture_file_error::open_failed);
        }
        bytes_reader reader{header};
        std::uint32_t magic = 0, payload_crc = 0, header_crc = 0;
        std::uint64_t size = 0;
        disk_record result;
        if (!reader.get(magic) || !reader.get(result.type) || !reader.get(result.sequence) ||
            !reader.get(size) || !reader.get(payload_crc) || !reader.get(header_crc) || magic != kRecordMagic)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        if (crc(std::span(header).first(28)) != header_crc)
        {
            return std::unexpected(capture_file_error::checksum_mismatch);
        }
        if (size > kMaximumRecordBytes || (result.type == kMetadataRecord && size > kMaximumMetadataBytes))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        if (size > file_size - offset - kEnvelopeBytes)
        {
            return std::unexpected(capture_file_error::truncated);
        }
        result.payload.resize(static_cast<std::size_t>(size));
        if (!read_at(input, offset + kEnvelopeBytes, result.payload))
        {
            return std::unexpected(capture_file_error::open_failed);
        }
        if (crc(result.payload) != payload_crc)
        {
            return std::unexpected(capture_file_error::checksum_mismatch);
        }
        result.next = offset + kEnvelopeBytes + size;
        return result;
    }

    std::uint64_t metadata_bytes(const capture_session& capture)
    {
        std::uint64_t bytes = sizeof(capture_session) + 256;
        for (const auto& marker : capture.markers())
        {
            bytes = add(bytes, sizeof(marker) + marker.name.capacity() + marker.file.capacity());
        }
        for (const auto& thread : capture.threads())
        {
            bytes = add(bytes, sizeof(thread) + thread.name.capacity());
        }
        for (const auto& counter : capture.counter_descriptors())
        {
            bytes = add(bytes, sizeof(counter) + counter.name.capacity() + counter.unit.capacity());
        }
        return bytes;
    }

    std::uint64_t batch_bytes(const capture_session& capture)
    {
        std::uint64_t bytes = metadata_bytes(capture);
        for (const auto& frame : capture.frames())
        {
            bytes = add(bytes, add(sizeof(frame), frame.memory_bytes()));
            // 공유 페이지와 구간 인덱스의 비용도 보수적으로 포함한다.
            // 같은 페이지를 참조하는 여러 프레임은 중복 계상할 수 있다.
            bytes = add(bytes, frame.events.size() * 64ull);
        }
        return bytes;
    }

    std::vector<std::byte> encode_metadata(const capture_session& capture)
    {
        capture_session metadata({}, {capture.threads().begin(), capture.threads().end()},
                                 {capture.markers().begin(), capture.markers().end()}, capture.environment(),
                                 false, 0, 0, {capture.counter_descriptors().begin(), capture.counter_descriptors().end()});
        return encode_capture(metadata);
    }

    std::expected<std::vector<std::byte>, capture_file_error> encode_frame(const frame_record& frame)
    {
        if (frame.events.size() > (kMaximumRecordBytes - kFrameBytes) / kEventBytes ||
            frame.counters.size() > (kMaximumRecordBytes - kFrameBytes - frame.events.size() * kEventBytes) / kCounterBytes)
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        bytes_writer output;
        output.bytes.reserve(kFrameBytes + frame.events.size() * kEventBytes + frame.counters.size() * kCounterBytes);
        output.put(frame.engine_frame);
        output.put(frame.tick_begin);
        output.put(frame.tick_end);
        output.put(frame.dropped_events);
        output.put(static_cast<std::uint32_t>(frame.events.size()));
        output.put(static_cast<std::uint32_t>(frame.counters.size()));
        for (const auto& event : frame.events)
        {
            output.put(event.tick_begin);
            output.put(event.tick_end);
            output.put(event.marker);
            output.put(event.frame);
            output.put(event.thread_slot);
            output.put(event.depth);
            output.put(static_cast<std::uint8_t>(event.flags));
            output.put(event.queue);
            output.put(event.submission);
            output.put(event.view);
            output.put(event.reserved);
            output.put(event.cpu.session);
            output.put(event.cpu.tick);
            output.put(event.cpu.task);
        }
        for (const auto& sample : frame.counters)
        {
            output.put(static_cast<std::uint16_t>(sample.id));
            output.put(std::bit_cast<std::uint64_t>(sample.value));
            output.put(sample.cpu.session);
            output.put(sample.cpu.tick);
            output.put(sample.cpu.task);
        }
        return std::move(output.bytes);
    }

    // 전체 개요 검증은 스택의 이벤트 하나만 재사용한다. 전체 이벤트 벡터를 만들지 않는다.
    std::expected<recording_overview_bin, capture_file_error>
    parse_frame(std::span<const std::byte> payload, const capture_session& metadata, frame_record* decoded = nullptr)
    {
        bytes_reader input{payload};
        std::bitset<65536> thread_slots;
        for (const auto& thread : metadata.threads())
        {
            if (thread.slot >= thread_slots.size())
            {
                return std::unexpected(capture_file_error::malformed);
            }
            thread_slots.set(thread.slot);
        }
        recording_overview_bin summary;
        std::uint32_t events = 0, counters = 0;
        if (!input.get(summary.first_engine_frame) || !input.get(summary.tick_begin) || !input.get(summary.tick_end) ||
            !input.get(summary.dropped_events) || !input.get(events) || !input.get(counters) ||
            summary.tick_end < summary.tick_begin || events > input.remaining() / kEventBytes ||
            counters > (input.remaining() - static_cast<std::size_t>(events) * kEventBytes) / kCounterBytes ||
            input.remaining() != static_cast<std::size_t>(events) * kEventBytes + static_cast<std::size_t>(counters) * kCounterBytes)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        summary.last_engine_frame = summary.first_engine_frame;
        summary.frame_count = 1;
        summary.event_count = events;
        summary.minimum_duration = summary.maximum_duration = summary.tick_end - summary.tick_begin;
        if (decoded != nullptr)
        {
            decoded->engine_frame = summary.first_engine_frame;
            decoded->tick_begin = summary.tick_begin;
            decoded->tick_end = summary.tick_end;
            decoded->dropped_events = summary.dropped_events;
            decoded->events.resize(events);
            decoded->counters.reserve(counters);
        }
        for (std::uint32_t index = 0; index < events; ++index)
        {
            profile_event event{};
            std::uint8_t flags = 0;
            if (!input.get(event.tick_begin) || !input.get(event.tick_end) || !input.get(event.marker) ||
                !input.get(event.frame) || !input.get(event.thread_slot) || !input.get(event.depth) ||
                !input.get(flags) || !input.get(event.queue) || !input.get(event.submission) ||
                !input.get(event.view) || !input.get(event.reserved) || !input.get(event.cpu.session) ||
                !input.get(event.cpu.tick) || !input.get(event.cpu.task) || (flags & ~0x0Fu) != 0 ||
                event.marker >= metadata.marker_count() || event.tick_end < event.tick_begin ||
                event.depth > kMaxScopeDepth ||
                !thread_slots.test(event.thread_slot) ||
                ((flags & static_cast<std::uint8_t>(event_flags::gpu_span)) == 0 &&
                 (event.tick_end <= summary.tick_begin || event.tick_end > summary.tick_end)) ||
                ((flags & static_cast<std::uint8_t>(event_flags::gpu_span)) != 0 && event.frame != summary.first_engine_frame) ||
                ((flags & static_cast<std::uint8_t>(event_flags::gpu_span)) != 0 &&
                 (event.cpu.session != 0 || event.cpu.tick != 0 || event.cpu.task != 0)))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            event.flags = static_cast<event_flags>(flags);
            if (decoded != nullptr)
            {
                decoded->events.mutable_at(index) = event;
            }
        }
        const std::size_t known_counters = metadata.counter_descriptors().empty() ? 5 : metadata.counter_descriptors().size();
        for (std::uint32_t index = 0; index < counters; ++index)
        {
            profile_counter_sample sample{};
            std::uint16_t id = 0;
            std::uint64_t bits = 0;
            if (!input.get(id) || !input.get(bits) || !input.get(sample.cpu.session) ||
                !input.get(sample.cpu.tick) || !input.get(sample.cpu.task) || id == 0 || id > known_counters)
            {
                return std::unexpected(capture_file_error::malformed);
            }
            sample.id = static_cast<profile_counter_id>(id);
            sample.value = std::bit_cast<double>(bits);
            if (!std::isfinite(sample.value))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            if (decoded != nullptr)
            {
                decoded->counters.push_back(sample);
            }
        }
        return summary;
    }

    std::expected<capture_session_ptr, capture_file_error> decode_metadata(std::span<const std::byte> payload)
    {
        if (payload.size() < 16 || payload.size() > kMaximumMetadataBytes ||
            std::memcmp(payload.data(), kMagic.data(), kMagic.size()) != 0)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        bytes_reader header{payload.subspan(8)};
        std::uint32_t version = 0, count = 0;
        if (!header.get(version) || !header.get(count) || version != kCaptureFileVersion || count != 6 ||
            payload.size() < 16 + count * 32)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        std::uint32_t seen = 0;
        std::array<std::pair<std::uint64_t, std::uint64_t>, 6> ranges{};
        for (std::uint32_t index = 0; index < count; ++index)
        {
            bytes_reader entry{payload.subspan(16 + index * 32, 32)};
            std::uint32_t type = 0, chunk_version = 0, checksum = 0, reserved = 0;
            std::uint64_t offset = 0, size = 0;
            if (!entry.get(type) || !entry.get(chunk_version) || !entry.get(offset) || !entry.get(size) ||
                !entry.get(checksum) || !entry.get(reserved) || reserved != 0 || type == 0 || type > 6 ||
                (seen & (1u << type)) != 0 || offset < 16 + count * 32 || offset > payload.size() || size > payload.size() - offset)
            {
                return std::unexpected(capture_file_error::malformed);
            }
            if (chunk_version != ((type == 3 || type == 4 || type == 5) ? 2u : 1u))
            {
                return std::unexpected(capture_file_error::unsupported_version);
            }
            for (std::uint32_t previous = 0; previous < index; ++previous)
            {
                if (offset < ranges[previous].second && ranges[previous].first < offset + size)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
            ranges[index] = {offset, offset + size};
            seen |= 1u << type;
            bytes_reader body{payload.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size))};
            std::uint32_t records = 0;
            if (type == 4)
            {
                if (size != 4 || !body.get(records) || records != 0)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
            else if (type == 5)
            {
                std::uint64_t dropped = 0;
                if (size != 12 || !body.get(dropped) || !body.get(records) || records != 0)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
            else if (type == 2 || type == 3 || type == 6)
            {
                if (!body.get(records) || records > 65536)
                {
                    return std::unexpected(capture_file_error::resource_limit);
                }
            }
        }
        auto result = decode_capture(payload);
        if (!result)
        {
            return result;
        }
        std::array<bool, 65536> slots{};
        for (const auto& thread : (*result)->threads())
        {
            if (thread.slot >= slots.size() || slots[thread.slot])
            {
                return std::unexpected(capture_file_error::malformed);
            }
            slots[thread.slot] = true;
        }
        return result;
    }

    std::vector<std::byte> encode_footer(const recording_status& status, bool complete, std::uint32_t unacked)
    {
        bytes_writer output;
        output.put(static_cast<std::uint8_t>(complete));
        output.put(unacked);
        output.put(status.written_frames);
        output.put(status.submitted_frames);
        output.put(status.first_tick);
        output.put(status.last_tick);
        output.put(status.dropped_frames);
        output.put(status.dropped_events);
        output.put(status.dropped_counters);
        output.put(status.source_dropped_counters);
        output.put(status.source_losses.dropped_events);
        output.put(status.source_losses.dropped_frame_boundaries);
        output.put(status.source_losses.late_events);
        output.put(status.source_losses.late_gpu_spans);
        return std::move(output.bytes);
    }

    bool has_losses(const recording_status& status)
    {
        return status.dropped_frames != 0 || status.dropped_events != 0 || status.dropped_counters != 0 ||
               status.source_dropped_counters != 0 || status.source_losses.dropped_events != 0 ||
               status.source_losses.dropped_frame_boundaries != 0 || status.source_losses.late_events != 0 ||
               status.source_losses.late_gpu_spans != 0;
    }

    bool parse_footer(std::span<const std::byte> payload, recording_status& status, bool& complete,
                      std::uint32_t& unacked)
    {
        bytes_reader input{payload};
        std::uint8_t flag = 0;
        const std::uint64_t written = status.written_frames;
        if (!input.get(flag) || flag > 1 || !input.get(unacked) || !input.get(status.written_frames) ||
            !input.get(status.submitted_frames) || !input.get(status.first_tick) || !input.get(status.last_tick) ||
            !input.get(status.dropped_frames) || !input.get(status.dropped_events) || !input.get(status.dropped_counters) ||
            !input.get(status.source_dropped_counters) || !input.get(status.source_losses.dropped_events) ||
            !input.get(status.source_losses.dropped_frame_boundaries) || !input.get(status.source_losses.late_events) ||
            !input.get(status.source_losses.late_gpu_spans) || input.remaining() != 0 ||
            status.written_frames != written || status.submitted_frames < written ||
            status.dropped_frames != status.submitted_frames - written || status.last_tick < status.first_tick)
        {
            return false;
        }
        complete = flag != 0 && unacked == 0 && !has_losses(status);
        return true;
    }

    void merge_bin(recording_overview_bin& target, const recording_overview_bin& source)
    {
        target.frame_count += source.frame_count;
        target.last_engine_frame = source.last_engine_frame;
        target.tick_end = source.tick_end;
        target.minimum_duration = (std::min)(target.minimum_duration, source.minimum_duration);
        target.maximum_duration = (std::max)(target.maximum_duration, source.maximum_duration);
        target.event_count = add(target.event_count, source.event_count);
        target.dropped_events = add(target.dropped_events, source.dropped_events);
    }
}

namespace ce
{
    struct continuous_capture_writer::implementation
    {
        std::filesystem::path path;
        recording_options options;
        mutable std::mutex mutex;
        std::condition_variable wake;
        std::deque<std::pair<capture_session_ptr, std::uint64_t>> queue;
        recording_status status;
        std::thread worker;
        capture_session_ptr active_batch;
        std::size_t active_index = 0;
        bool finalize_requested = false;
        bool flush_requested = false;
        bool source_complete = false;
        std::uint32_t unacked = 0;
        std::uint32_t last_submitted_frame = 0;
        bool has_submitted_frame = false;

        void reject(const capture_session& batch)
        {
            using namespace detail::recording_file_impl;
            status.dropped_frames = add(status.dropped_frames, batch.frame_count());
            status.dropped_events = add(status.dropped_events, batch.total_events());
            for (const auto& frame : batch.frames())
            {
                status.dropped_counters = add(status.dropped_counters, frame.counters.size());
            }
        }

        void fail(capture_file_error error)
        {
            std::lock_guard lock(mutex);
            if (active_batch)
            {
                using namespace detail::recording_file_impl;
                for (; active_index < active_batch->frames().size(); ++active_index)
                {
                    const auto& frame = active_batch->frames()[active_index];
                    status.dropped_frames = add(status.dropped_frames, 1);
                    status.dropped_events = add(status.dropped_events, frame.events.size());
                    status.dropped_counters = add(status.dropped_counters, frame.counters.size());
                }
                active_batch.reset();
            }
            for (const auto& entry : queue)
            {
                reject(*entry.first);
            }
            queue.clear();
            status.queued_bytes = 0;
            status.queued_batches = 0;
            status.state = recording_state::failed;
            status.error = error;
            wake.notify_all();
        }

        void run()
        {
            using namespace detail::recording_file_impl;
            try
            {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                const auto header = file_header();
                if (!output || !write_bytes(output, header))
                {
                    fail(capture_file_error::write_failed);
                    return;
                }
                std::uint64_t written = header.size();
                std::uint64_t sequence = 0;
                std::vector<std::byte> last_metadata;
                {
                    std::lock_guard lock(mutex);
                    status.written_bytes = written;
                    if (status.state != recording_state::flushing)
                    {
                        status.state = recording_state::recording;
                    }
                }
                auto last_flush = std::chrono::steady_clock::now();
                for (;;)
                {
                    std::pair<capture_session_ptr, std::uint64_t> entry;
                    bool finalize = false;
                    bool flush = false;
                    {
                        std::unique_lock lock(mutex);
                        wake.wait_for(lock, std::chrono::seconds(1), [&]
                        {
                            return !queue.empty() || finalize_requested || flush_requested;
                        });
                        if (!queue.empty())
                        {
                            entry = std::move(queue.front());
                            queue.pop_front();
                        }
                        finalize = finalize_requested && !entry.first && queue.empty();
                        flush = std::exchange(flush_requested, false);
                    }
                    if (entry.first)
                    {
                        active_batch = entry.first;
                        active_index = 0;
                        auto metadata = encode_metadata(*entry.first);
                        if (metadata.size() > kMaximumMetadataBytes)
                        {
                            fail(capture_file_error::resource_limit);
                            return;
                        }
                        if (metadata != last_metadata && !write_record(output, kMetadataRecord, sequence++, metadata, written))
                        {
                            fail(capture_file_error::write_failed);
                            return;
                        }
                        last_metadata = std::move(metadata);
                        for (const auto& frame : entry.first->frames())
                        {
                            auto payload = encode_frame(frame);
                            if (!payload)
                            {
                                std::lock_guard lock(mutex);
                                ++status.dropped_frames;
                                status.dropped_events = add(status.dropped_events, frame.events.size());
                                status.dropped_counters = add(status.dropped_counters, frame.counters.size());
                                ++active_index;
                                continue;
                            }
                            if (!write_record(output, kFrameRecord, sequence++, *payload, written))
                            {
                                fail(capture_file_error::write_failed);
                                return;
                            }
                            {
                                std::lock_guard lock(mutex);
                                ++status.written_frames;
                                status.written_bytes = written;
                                ++active_index;
                            }
                        }
                        active_batch.reset();
                        entry.first.reset();
                        {
                            std::lock_guard lock(mutex);
                            status.written_bytes = written;
                            status.queued_bytes -= entry.second;
                            --status.queued_batches;
                            wake.notify_all();
                        }
                    }
                    if (finalize)
                    {
                        recording_status final_status;
                        bool complete = false;
                        std::uint32_t unacked_streams = 0;
                        {
                            std::lock_guard lock(mutex);
                            final_status = status;
                            complete = source_complete && unacked == 0 && !has_losses(status);
                            unacked_streams = unacked;
                        }
                        if (last_metadata.empty())
                        {
                            capture_session empty({}, {}, {}, {}, false, unacked_streams);
                            auto metadata = encode_metadata(empty);
                            if (!write_record(output, kMetadataRecord, sequence++, metadata, written))
                            {
                                fail(capture_file_error::write_failed);
                                return;
                            }
                        }
                        const auto footer = encode_footer(final_status, complete, unacked_streams);
                        if (!write_record(output, kFinalizeRecord, sequence++, footer, written))
                        {
                            fail(capture_file_error::write_failed);
                            return;
                        }
                        output.flush();
                        output.close();
                        if (!output)
                        {
                            fail(capture_file_error::write_failed);
                            return;
                        }
                        std::lock_guard lock(mutex);
                        status.written_bytes = status.flushed_bytes = written;
                        status.state = recording_state::finalized;
                        return;
                    }
                    if (flush || std::chrono::steady_clock::now() - last_flush >= std::chrono::seconds(1))
                    {
                        output.flush();
                        if (!output)
                        {
                            fail(capture_file_error::write_failed);
                            return;
                        }
                        last_flush = std::chrono::steady_clock::now();
                        std::lock_guard lock(mutex);
                        status.flushed_bytes = written;
                    }
                }
            }
            catch (...)
            {
                fail(capture_file_error::write_failed);
            }
        }
    };

    continuous_capture_writer::continuous_capture_writer(std::unique_ptr<implementation> implementation)
        : implementation_(std::move(implementation))
    {
    }

    continuous_capture_writer::~continuous_capture_writer()
    {
        if (implementation_)
        {
            request_finalize(false, 0);
            wait();
        }
    }

    std::expected<std::unique_ptr<continuous_capture_writer>, capture_file_error>
    continuous_capture_writer::start(const std::filesystem::path& path, recording_options options)
    {
        using namespace detail::recording_file_impl;
        if (options.max_queued_bytes == 0 || options.max_queued_batches == 0)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        try
        {
            auto state = std::make_unique<implementation>();
            state->options = options;
            state->path = path;
            if (state->path.empty())
            {
                // 고유 디렉터리를 원자적으로 확보한다. 중단된 임시 녹화는 복구를 위해 남긴다.
                std::error_code error;
                const auto root = std::filesystem::temp_directory_path(error);
                if (error)
                {
                    return std::unexpected(capture_file_error::open_failed);
                }
                static std::atomic<std::uint64_t> next_id{0};
                bool created = false;
                for (std::uint32_t attempt = 0; attempt < 32 && !created; ++attempt)
                {
                    auto directory = root / ("ceprof-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                              "-" + std::to_string(next_id.fetch_add(1)));
                    created = std::filesystem::create_directory(directory, error);
                    if (created)
                    {
                        state->path = directory / "recording.ceprof";
                    }
                }
                if (!created)
                {
                    return std::unexpected(capture_file_error::open_failed);
                }
            }
            else
            {
                std::error_code error;
                if (std::filesystem::exists(state->path, error) || error)
                {
                    return std::unexpected(capture_file_error::write_failed);
                }
            }
            auto result = std::unique_ptr<continuous_capture_writer>(new continuous_capture_writer(std::move(state)));
            auto* implementation = result->implementation_.get();
            implementation->worker = std::thread([implementation] { implementation->run(); });
            return result;
        }
        catch (...)
        {
            return std::unexpected(capture_file_error::open_failed);
        }
    }

    bool continuous_capture_writer::append(capture_session_ptr batch)
    {
        return append_impl(std::move(batch), false);
    }

    bool continuous_capture_writer::append_final(capture_session_ptr batch)
    {
        return append_impl(std::move(batch), true);
    }

    bool continuous_capture_writer::append_impl(capture_session_ptr batch, bool wait_for_capacity)
    {
        using namespace detail::recording_file_impl;
        if (!batch)
        {
            return false;
        }
        auto& state = *implementation_;
        const auto bytes = batch_bytes(*batch);
        std::unique_lock lock(state.mutex);
        if (state.finalize_requested)
        {
            return false;
        }
        bool ordered = true;
        for (const auto& frame : batch->frames())
        {
            if ((state.has_submitted_frame && frame.engine_frame <= state.last_submitted_frame) ||
                frame.tick_end < frame.tick_begin || (state.has_submitted_frame && frame.tick_begin < state.status.last_tick))
            {
                ordered = false;
            }
            if (!state.has_submitted_frame)
            {
                state.status.first_tick = frame.tick_begin;
            }
            state.has_submitted_frame = true;
            state.last_submitted_frame = frame.engine_frame;
            state.status.last_tick = (std::max)(state.status.last_tick, frame.tick_end);
        }
        state.status.submitted_frames = add(state.status.submitted_frames, batch->frame_count());
        if (state.status.state == recording_state::failed || !ordered ||
            batch->marker_count() > 65536 || batch->threads().size() > 65536 ||
            batch->counter_descriptors().size() > 65535 || metadata_bytes(*batch) > kMaximumMetadataBytes || bytes > state.options.max_queued_bytes)
        {
            state.reject(*batch);
            return false;
        }
        if (wait_for_capacity)
        {
            state.status.state = recording_state::flushing;
            state.wake.wait(lock, [&]
            {
                return state.status.state == recording_state::failed || state.finalize_requested ||
                    (state.status.queued_batches < state.options.max_queued_batches &&
                     state.status.queued_bytes <= state.options.max_queued_bytes - bytes);
            });
        }
        if (state.status.state == recording_state::failed || state.finalize_requested ||
            state.status.queued_batches >= state.options.max_queued_batches ||
            state.status.queued_bytes > state.options.max_queued_bytes - bytes)
        {
            state.reject(*batch);
            return false;
        }
        try
        {
            state.queue.emplace_back(batch, bytes);
        }
        catch (...)
        {
            state.reject(*batch);
            return false;
        }
        state.status.queued_bytes += bytes;
        ++state.status.queued_batches;
        state.wake.notify_one();
        return true;
    }

    void continuous_capture_writer::request_flush()
    {
        std::lock_guard lock(implementation_->mutex);
        implementation_->flush_requested = true;
        implementation_->wake.notify_one();
    }

    void continuous_capture_writer::request_finalize(bool complete, std::uint32_t unacked_streams,
                                                      std::uint64_t dropped_counters, recording_source_losses source_losses)
    {
        std::lock_guard lock(implementation_->mutex);
        auto& state = *implementation_;
        if (state.finalize_requested || state.status.state == recording_state::finalized)
        {
            return;
        }
        state.finalize_requested = true;
        state.source_complete = complete;
        state.unacked = unacked_streams;
        state.status.source_dropped_counters = dropped_counters;
        state.status.source_losses = source_losses;
        if (state.status.state != recording_state::failed)
        {
            state.status.state = recording_state::flushing;
        }
        state.wake.notify_one();
    }

    recording_status continuous_capture_writer::status() const
    {
        std::lock_guard lock(implementation_->mutex);
        return implementation_->status;
    }

    const std::filesystem::path& continuous_capture_writer::path() const
    {
        return implementation_->path;
    }

    void continuous_capture_writer::wait()
    {
        if (implementation_->worker.joinable())
        {
            implementation_->worker.join();
        }
    }

    struct capture_recording::implementation
    {
        std::filesystem::path path;
        capture_session_ptr metadata;
        std::vector<recording_overview_bin> bins;
        std::vector<std::uint64_t> offsets;
        std::uint64_t stride = 1;
        std::uint64_t frames = 0;
        std::uint64_t valid_bytes = 0;
        recording_status status;
        bool finalized = false;
        bool recovered = false;
        bool complete = false;
        std::uint32_t unacked = 0;
        // v1/v2 파일은 프레임과 카운터가 별도 청크에 있으므로 각각 희소 인덱스를 둔다.
        std::uint32_t legacy_frame_version = 0;
        std::uint32_t legacy_counter_version = 0;
        std::uint64_t legacy_frames_end = 0;
        std::uint64_t legacy_counters_offset = 0;
        std::uint64_t legacy_counters_end = 0;
        std::vector<std::uint64_t> legacy_counter_offsets;

        void add_frame(recording_overview_bin bin, std::uint64_t offset)
        {
            using namespace detail::recording_file_impl;
            bin.first_ordinal = frames++;
            if (!bins.empty() && bins.back().frame_count < stride)
            {
                merge_bin(bins.back(), bin);
                return;
            }
            if (bins.size() == kMaximumBins)
            {
                for (std::size_t i = 0; i < kMaximumBins / 2; ++i)
                {
                    bins[i] = bins[i * 2];
                    merge_bin(bins[i], bins[i * 2 + 1]);
                    offsets[i] = offsets[i * 2];
                }
                bins.resize(kMaximumBins / 2);
                offsets.resize(kMaximumBins / 2);
                stride *= 2;
            }
            bins.push_back(bin);
            offsets.push_back(offset);
        }
    };

    capture_recording::capture_recording(std::shared_ptr<implementation> implementation)
        : implementation_(std::move(implementation))
    {
    }

    std::uint64_t capture_recording::frame_count() const { return implementation_->frames; }
    std::span<const recording_overview_bin> capture_recording::overview() const { return implementation_->bins; }
    const std::filesystem::path& capture_recording::path() const { return implementation_->path; }
    capture_session_ptr capture_recording::metadata() const { return implementation_->metadata; }
    bool capture_recording::finalized() const { return implementation_->finalized; }
    bool capture_recording::recovered() const { return implementation_->recovered; }
    bool capture_recording::complete() const { return implementation_->complete; }
    recording_status capture_recording::status() const { return implementation_->status; }
    std::uint64_t capture_recording::valid_bytes() const { return implementation_->valid_bytes; }
}

namespace ce::detail::recording_file_impl
{
    struct legacy_chunk
    {
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::uint32_t checksum = 0;
    };

    std::expected<std::vector<legacy_chunk>, capture_file_error>
    legacy_table(std::ifstream& input, std::uint64_t size, std::uint32_t count, std::stop_token cancel)
    {
        if (count > 64)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        if (size < kHeaderBytes + static_cast<std::uint64_t>(count) * 32)
        {
            return std::unexpected(capture_file_error::truncated);
        }
        std::vector<legacy_chunk> chunks;
        std::uint32_t seen = 0;
        std::array<std::byte, 64 * 1024> scratch{};
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::array<std::byte, 32> bytes{};
            if (!read_at(input, kHeaderBytes + index * 32ull, bytes))
            {
                return std::unexpected(capture_file_error::open_failed);
            }
            bytes_reader reader{bytes};
            legacy_chunk chunk;
            std::uint32_t reserved = 0;
            if (!reader.get(chunk.type) || !reader.get(chunk.version) || !reader.get(chunk.offset) ||
                !reader.get(chunk.size) || !reader.get(chunk.checksum) || !reader.get(reserved) || reserved != 0 ||
                chunk.offset < kHeaderBytes + count * 32ull)
            {
                return std::unexpected(capture_file_error::malformed);
            }
            if (chunk.offset > size || chunk.size > size - chunk.offset)
            {
                return std::unexpected(capture_file_error::truncated);
            }
            if (chunk.version == 0 || chunk.version > ((chunk.type == 3 || chunk.type == 4 || chunk.type == 5) ? 2u : 1u))
            {
                return std::unexpected(capture_file_error::unsupported_version);
            }
            if (chunk.type >= 1 && chunk.type <= 6)
            {
                const std::uint32_t bit = 1u << chunk.type;
                if ((seen & bit) != 0)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
                seen |= bit;
            }
            for (const auto& existing : chunks)
            {
                if (chunk.offset < existing.offset + existing.size && existing.offset < chunk.offset + chunk.size)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
            std::uint64_t offset = chunk.offset;
            std::uint64_t remaining = chunk.size;
            std::uint32_t checksum = 0xFFFFFFFFu;
            while (remaining != 0)
            {
                if (cancel.stop_requested())
                {
                    return std::unexpected(capture_file_error::canceled);
                }
                const auto amount = static_cast<std::size_t>((std::min)(remaining, static_cast<std::uint64_t>(scratch.size())));
                auto bytes = std::span(scratch).first(amount);
                if (!read_at(input, offset, bytes))
                {
                    return std::unexpected(capture_file_error::open_failed);
                }
                checksum = update_crc(checksum, bytes);
                remaining -= amount;
                offset += amount;
            }
            if ((checksum ^ 0xFFFFFFFFu) != chunk.checksum)
            {
                return std::unexpected(capture_file_error::checksum_mismatch);
            }
            chunks.push_back(chunk);
        }
        if ((seen & 0x1Eu) != 0x1Eu)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        return chunks;
    }

    std::expected<capture_session_ptr, capture_file_error>
    legacy_metadata(std::ifstream& input, std::span<const legacy_chunk> chunks)
    {
        std::vector<std::pair<legacy_chunk, std::vector<std::byte>>> metadata;
        std::uint64_t total = 0;
        for (auto chunk : chunks)
        {
            if (chunk.type != 1 && chunk.type != 2 && chunk.type != 3 && chunk.type != 6)
            {
                continue;
            }
            total += chunk.size;
            if (total > kMaximumMetadataBytes)
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(chunk.size));
            if (!read_at(input, chunk.offset, bytes))
            {
                return std::unexpected(capture_file_error::open_failed);
            }
            if (chunk.type == 2 || chunk.type == 3 || chunk.type == 6)
            {
                bytes_reader body{bytes};
                std::uint32_t records = 0;
                if (!body.get(records) || records > 65536)
                {
                    return std::unexpected(capture_file_error::resource_limit);
                }
            }
            metadata.emplace_back(chunk, std::move(bytes));
        }
        std::vector<std::byte> empty_frames(4);
        metadata.push_back({legacy_chunk{4, 2, 0, 4, crc(empty_frames)}, std::move(empty_frames)});
        bytes_writer result;
        for (char value : kMagic)
        {
            result.put(static_cast<std::uint8_t>(value));
        }
        result.put(kCaptureFileVersion);
        result.put(static_cast<std::uint32_t>(metadata.size()));
        std::uint64_t offset = kHeaderBytes + metadata.size() * 32;
        for (const auto& [chunk, bytes] : metadata)
        {
            result.put(chunk.type);
            result.put(chunk.version);
            result.put(offset);
            result.put(static_cast<std::uint64_t>(bytes.size()));
            result.put(chunk.checksum);
            result.put(std::uint32_t{0});
            offset += bytes.size();
        }
        for (const auto& entry : metadata)
        {
            result.bytes.insert(result.bytes.end(), entry.second.begin(), entry.second.end());
        }
        return decode_capture(result.bytes);
    }

    struct legacy_frame_header
    {
        recording_overview_bin bin;
        std::uint32_t events = 0;
        std::uint64_t next = 0;
    };

    std::expected<legacy_frame_header, capture_file_error>
    read_legacy_frame(std::ifstream& input, std::uint64_t offset, std::uint64_t end, std::uint32_t version)
    {
        std::array<std::byte, 32> bytes{};
        if (offset > end || end - offset < bytes.size() || !read_at(input, offset, bytes))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        bytes_reader reader{bytes};
        legacy_frame_header frame;
        if (!reader.get(frame.bin.first_engine_frame) || !reader.get(frame.bin.tick_begin) ||
            !reader.get(frame.bin.tick_end) || !reader.get(frame.bin.dropped_events) || !reader.get(frame.events) ||
            frame.bin.tick_end < frame.bin.tick_begin)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        const std::size_t event_bytes = version >= 2 ? 62 : 38;
        if (frame.events > (end - offset - bytes.size()) / event_bytes)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        frame.next = offset + bytes.size() + frame.events * static_cast<std::uint64_t>(event_bytes);
        frame.bin.last_engine_frame = frame.bin.first_engine_frame;
        frame.bin.frame_count = 1;
        frame.bin.event_count = frame.events;
        frame.bin.minimum_duration = frame.bin.maximum_duration = frame.bin.tick_end - frame.bin.tick_begin;
        return frame;
    }

    std::expected<frame_record, capture_file_error>
    decode_legacy_frame(std::ifstream& input, std::uint64_t offset, const legacy_frame_header& header,
                        std::uint32_t version, const capture_session& metadata)
    {
        std::vector<std::byte> bytes(static_cast<std::size_t>(header.next - offset - 32));
        if (!read_at(input, offset + 32, bytes))
        {
            return std::unexpected(capture_file_error::open_failed);
        }
        std::bitset<65536> thread_slots;
        for (const auto& thread : metadata.threads())
        {
            if (thread.slot >= thread_slots.size())
            {
                return std::unexpected(capture_file_error::malformed);
            }
            thread_slots.set(thread.slot);
        }
        frame_record frame;
        frame.engine_frame = header.bin.first_engine_frame;
        frame.tick_begin = header.bin.tick_begin;
        frame.tick_end = header.bin.tick_end;
        frame.dropped_events = header.bin.dropped_events;
        frame.events.resize(header.events);
        bytes_reader reader{bytes};
        for (std::uint32_t index = 0; index < header.events; ++index)
        {
            profile_event& event = frame.events.mutable_at(index);
            std::uint8_t flags = 0;
            if (!reader.get(event.tick_begin) || !reader.get(event.tick_end) || !reader.get(event.marker) ||
                !reader.get(event.frame) || !reader.get(event.thread_slot) || !reader.get(event.depth) ||
                !reader.get(flags) || !reader.get(event.queue) || !reader.get(event.submission) ||
                !reader.get(event.view) || !reader.get(event.reserved) ||
                (version >= 2 && (!reader.get(event.cpu.session) || !reader.get(event.cpu.tick) || !reader.get(event.cpu.task))) ||
                (flags & ~0x0Fu) != 0 || event.marker >= metadata.marker_count() || event.tick_end < event.tick_begin ||
                event.depth > kMaxScopeDepth || !thread_slots.test(event.thread_slot) ||
                ((flags & static_cast<std::uint8_t>(event_flags::gpu_span)) != 0 &&
                 (event.cpu.session != 0 || event.cpu.tick != 0 || event.cpu.task != 0)))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            event.flags = static_cast<event_flags>(flags);
        }
        return frame;
    }

    bool compatible_metadata(const capture_session& previous, const capture_session& next)
    {
        if (previous.threads().size() > next.threads().size() ||
            previous.environment().ticks_per_second != next.environment().ticks_per_second ||
            previous.marker_count() > next.marker_count() ||
            previous.counter_descriptors().size() > next.counter_descriptors().size())
        {
            return false;
        }
        for (std::size_t index = 0; index < previous.threads().size(); ++index)
        {
            const auto& left = previous.threads()[index];
            const auto& right = next.threads()[index];
            if (left.slot != right.slot || left.os_thread_id != right.os_thread_id || left.kind != right.kind ||
                left.track_order != right.track_order || left.name != right.name)
            {
                return false;
            }
        }
        for (std::uint32_t index = 0; index < previous.marker_count(); ++index)
        {
            const auto& left = previous.markers()[index];
            const auto& right = next.markers()[index];
            if (left.kind != right.kind || left.line != right.line || left.name != right.name || left.file != right.file)
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < previous.counter_descriptors().size(); ++index)
        {
            const auto& left = previous.counter_descriptors()[index];
            const auto& right = next.counter_descriptors()[index];
            if (left.id != right.id || left.name != right.name || left.unit != right.unit || left.category != right.category)
            {
                return false;
            }
        }
        return true;
    }

    bool replace_file(const std::filesystem::path& temporary, const std::filesystem::path& destination)
    {
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        return !error;
#endif
    }
}

namespace ce
{
    std::expected<capture_recording_ptr, capture_file_error> open_capture_recording(const std::filesystem::path& path, std::stop_token cancel)
    {
        using namespace detail::recording_file_impl;
        if (cancel.stop_requested())
        {
            return std::unexpected(capture_file_error::canceled);
        }
        try
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 0)
            {
                return std::unexpected(capture_file_error::open_failed);
            }
            const auto size = static_cast<std::uint64_t>(input.tellg());
            std::array<std::byte, kHeaderBytes> header{};
            if (size < header.size() || !read_at(input, 0, header))
            {
                return std::unexpected(capture_file_error::truncated);
            }
            if (std::memcmp(header.data(), kMagic.data(), kMagic.size()) != 0)
            {
                return std::unexpected(capture_file_error::not_a_capture);
            }
            bytes_reader reader{std::span(header).subspan(8)};
            std::uint32_t version = 0, count = 0;
            reader.get(version);
            reader.get(count);
            if (version == 0 || version > kRecordingFileVersion)
            {
                return std::unexpected(capture_file_error::unsupported_version);
            }
            auto state = std::make_shared<capture_recording::implementation>();
            state->path = path;
            if (version <= kCaptureFileVersion)
            {
                const auto chunks = legacy_table(input, size, count, cancel);
                if (!chunks)
                {
                    return std::unexpected(chunks.error());
                }
                const auto metadata = legacy_metadata(input, *chunks);
                if (!metadata)
                {
                    return std::unexpected(metadata.error());
                }
                state->metadata = *metadata;
                state->complete = state->metadata->complete();
                state->unacked = state->metadata->unacked_streams();
                std::uint64_t frame_start = 0;
                for (const auto& chunk : *chunks)
                {
                    if (chunk.type == 4)
                    {
                        std::array<std::byte, 4> bytes{};
                        if (chunk.size < 4 || !read_at(input, chunk.offset, bytes))
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        bytes_reader count_reader{bytes};
                        std::uint32_t frames = 0;
                        count_reader.get(frames);
                        if (frames > (chunk.size - 4) / 32)
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        frame_start = chunk.offset + 4;
                        std::uint64_t offset = frame_start;
                        state->legacy_frame_version = chunk.version;
                        state->legacy_frames_end = chunk.offset + chunk.size;
                        for (std::uint32_t index = 0; index < frames; ++index)
                        {
                            if (cancel.stop_requested())
                            {
                                return std::unexpected(capture_file_error::canceled);
                            }
                            const auto frame = read_legacy_frame(input, offset, state->legacy_frames_end, chunk.version);
                            if (!frame || (!state->bins.empty() && frame->bin.first_engine_frame <= state->bins.back().last_engine_frame))
                            {
                                return std::unexpected(frame ? capture_file_error::malformed : frame.error());
                            }
                            state->add_frame(frame->bin, offset);
                            offset = frame->next;
                        }
                        if (offset != state->legacy_frames_end)
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                    }
                }
                for (const auto& chunk : *chunks)
                {
                    if (chunk.type != 5)
                    {
                        continue;
                    }
                    std::array<std::byte, 12> bytes{};
                    if (chunk.size < bytes.size() || !read_at(input, chunk.offset, bytes))
                    {
                        return std::unexpected(capture_file_error::malformed);
                    }
                    bytes_reader count_reader{bytes};
                    std::uint32_t frames = 0;
                    count_reader.get(state->status.source_dropped_counters);
                    count_reader.get(frames);
                    if (frames != state->frames || frames > (chunk.size - bytes.size()) / 8)
                    {
                        return std::unexpected(capture_file_error::malformed);
                    }
                    state->legacy_counter_version = chunk.version;
                    state->legacy_counters_offset = chunk.offset + bytes.size();
                    state->legacy_counters_end = chunk.offset + chunk.size;
                    std::uint64_t offset = state->legacy_counters_offset;
                    std::uint64_t frame_offset = frame_start;
                    std::size_t counter_bin = 0;
                    state->legacy_counter_offsets.resize(state->bins.size());
                    for (std::uint32_t index = 0; index < frames; ++index)
                    {
                        if (cancel.stop_requested())
                        {
                            return std::unexpected(capture_file_error::canceled);
                        }
                        if (counter_bin < state->bins.size() && state->bins[counter_bin].first_ordinal == index)
                        {
                            state->legacy_counter_offsets[counter_bin++] = offset;
                        }
                        std::array<std::byte, 8> counter_header{};
                        if (offset > state->legacy_counters_end || state->legacy_counters_end - offset < 8 ||
                            !read_at(input, offset, counter_header))
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        const auto frame = read_legacy_frame(input, frame_offset, state->legacy_frames_end, state->legacy_frame_version);
                        bytes_reader counter_reader{counter_header};
                        std::uint32_t engine_frame = 0, samples = 0;
                        counter_reader.get(engine_frame);
                        counter_reader.get(samples);
                        const std::size_t sample_bytes = chunk.version >= 2 ? 34 : 10;
                        if (!frame || engine_frame != frame->bin.first_engine_frame ||
                            samples > (state->legacy_counters_end - offset - 8) / sample_bytes)
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        offset += 8 + samples * static_cast<std::uint64_t>(sample_bytes);
                        frame_offset = frame->next;
                    }
                    if (offset != state->legacy_counters_end)
                    {
                        return std::unexpected(capture_file_error::malformed);
                    }
                }
                for (const auto& bin : state->bins)
                {
                    state->status.source_losses.dropped_events = add(state->status.source_losses.dropped_events, bin.dropped_events);
                }
                state->complete = state->complete && !has_losses(state->status);
                state->finalized = true;
                state->valid_bytes = size;
                state->status.written_frames = state->status.submitted_frames = state->frames;
                if (!state->bins.empty())
                {
                    state->status.first_tick = state->bins.front().tick_begin;
                    state->status.last_tick = state->bins.back().tick_end;
                }
            }
            else
            {
                if (count != 0)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
                std::uint64_t offset = kHeaderBytes;
                std::uint64_t sequence = 0;
                state->valid_bytes = offset;
                while (offset != size)
                {
                    if (cancel.stop_requested())
                    {
                        return std::unexpected(capture_file_error::canceled);
                    }
                    if (state->finalized)
                    {
                        return std::unexpected(capture_file_error::malformed);
                    }
                    auto record = read_record(input, offset, size);
                    if (!record)
                    {
                        if (record.error() == capture_file_error::truncated)
                        {
                            state->recovered = true;
                            break;
                        }
                        return std::unexpected(record.error());
                    }
                    if (record->sequence != sequence++ || state->finalized)
                    {
                        return std::unexpected(capture_file_error::malformed);
                    }
                    if (record->type == kMetadataRecord)
                    {
                        const auto metadata = decode_metadata(record->payload);
                        if (!metadata || (*metadata)->frame_count() != 0 ||
                            (state->metadata && !compatible_metadata(*state->metadata, **metadata)))
                        {
                            return std::unexpected(metadata ? capture_file_error::malformed : metadata.error());
                        }
                        state->metadata = *metadata;
                    }
                    else if (record->type == kFrameRecord)
                    {
                        if (!state->metadata || state->metadata->environment().ticks_per_second == 0)
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        const auto frame = parse_frame(record->payload, *state->metadata);
                        if (!frame || (!state->bins.empty() &&
                            (frame->first_engine_frame <= state->bins.back().last_engine_frame ||
                             frame->tick_begin < state->bins.back().tick_end)))
                        {
                            return std::unexpected(frame ? capture_file_error::malformed : frame.error());
                        }
                        state->add_frame(*frame, offset);
                        ++state->status.written_frames;
                    }
                    else if (record->type == kFinalizeRecord)
                    {
                        if (!state->metadata || !parse_footer(record->payload, state->status, state->complete, state->unacked) ||
                            (!state->bins.empty() && (state->status.first_tick > state->bins.front().tick_begin ||
                                                      state->status.last_tick < state->bins.back().tick_end)))
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        state->finalized = true;
                    }
                    else
                    {
                        return std::unexpected(capture_file_error::unsupported_version);
                    }
                    offset = record->next;
                    state->valid_bytes = offset;
                }
                if (!state->metadata)
                {
                    return std::unexpected(capture_file_error::truncated);
                }
                if (!state->finalized)
                {
                    state->recovered = true;
                    state->complete = false;
                    state->status.submitted_frames = state->frames;
                    if (!state->bins.empty())
                    {
                        state->status.first_tick = state->bins.front().tick_begin;
                        state->status.last_tick = state->bins.back().tick_end;
                    }
                }
            }
            state->status.state = state->finalized ? recording_state::finalized : recording_state::flushing;
            state->status.written_bytes = state->status.flushed_bytes = state->valid_bytes;
            return capture_recording_ptr(new capture_recording(std::move(state)));
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        catch (...)
        {
            return std::unexpected(capture_file_error::open_failed);
        }
    }

    std::expected<capture_session_ptr, capture_file_error>
    capture_recording::load_range(std::uint64_t first_ordinal, std::uint32_t count, std::size_t max_bytes,
                                  std::stop_token cancel) const
    {
        using namespace detail::recording_file_impl;
        if (cancel.stop_requested())
        {
            return std::unexpected(capture_file_error::canceled);
        }
        const auto& state = *implementation_;
        if (first_ordinal > state.frames || count > state.frames - first_ordinal)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        try
        {
            const std::uint64_t base_bytes = metadata_bytes(*state.metadata);
            if (base_bytes > max_bytes || count > (max_bytes - base_bytes) / sizeof(frame_record))
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
            std::uint64_t remaining = max_bytes - base_bytes - static_cast<std::uint64_t>(count) * sizeof(frame_record);
            std::vector<frame_record> frames;
            frames.reserve(count);
            if (count != 0)
            {
                auto bin = std::upper_bound(state.bins.begin(), state.bins.end(), first_ordinal,
                    [](std::uint64_t ordinal, const recording_overview_bin& value) { return ordinal < value.first_ordinal; });
                --bin;
                const auto bin_index = static_cast<std::size_t>(bin - state.bins.begin());
                std::uint64_t offset = state.offsets[bin_index];
                std::uint64_t ordinal = bin->first_ordinal;
                const std::uint64_t end_ordinal = first_ordinal + count;
                std::ifstream input(state.path, std::ios::binary);
                if (!input)
                {
                    return std::unexpected(capture_file_error::open_failed);
                }
                while (ordinal < end_ordinal)
                {
                    if (cancel.stop_requested())
                    {
                        return std::unexpected(capture_file_error::canceled);
                    }
                    if (state.legacy_frame_version != 0)
                    {
                        const auto header = read_legacy_frame(input, offset, state.legacy_frames_end, state.legacy_frame_version);
                        if (!header)
                        {
                            return std::unexpected(header.error());
                        }
                        if (ordinal >= first_ordinal)
                        {
                            const auto bytes = static_cast<std::uint64_t>(header->events) * (sizeof(profile_event) + kEventBytes);
                            if (bytes > remaining)
                            {
                                return std::unexpected(capture_file_error::resource_limit);
                            }
                            remaining -= bytes;
                            auto frame = decode_legacy_frame(input, offset, *header, state.legacy_frame_version, *state.metadata);
                            if (!frame)
                            {
                                return std::unexpected(frame.error());
                            }
                            frames.push_back(std::move(*frame));
                        }
                        offset = header->next;
                        ++ordinal;
                    }
                    else
                    {
                        auto record = read_record(input, offset, state.valid_bytes);
                        if (!record)
                        {
                            return std::unexpected(record.error());
                        }
                        offset = record->next;
                        if (record->type != kFrameRecord)
                        {
                            continue;
                        }
                        if (ordinal >= first_ordinal)
                        {
                            // 입력 버퍼와 해석된 레코드, 공유 소유권·구간 인덱스 비용을
                            // 직렬화 크기의 세 배로 보수적으로 예약한다.
                            const auto bytes = static_cast<std::uint64_t>(record->payload.size()) * 3;
                            if (bytes > remaining)
                            {
                                return std::unexpected(capture_file_error::resource_limit);
                            }
                            remaining -= bytes;
                            frame_record frame;
                            if (const auto parsed = parse_frame(record->payload, *state.metadata, &frame); !parsed)
                            {
                                return std::unexpected(parsed.error());
                            }
                            frames.push_back(std::move(frame));
                        }
                        ++ordinal;
                    }
                }
                if (state.legacy_counter_version != 0)
                {
                    std::uint64_t counter_offset = state.legacy_counter_offsets[bin_index];
                    const std::size_t sample_bytes = state.legacy_counter_version >= 2 ? 34 : 10;
                    const std::size_t known = state.metadata->counter_descriptors().empty() ? 5 : state.metadata->counter_descriptors().size();
                    for (std::uint64_t index = bin->first_ordinal; index < end_ordinal; ++index)
                    {
                        std::array<std::byte, 8> header{};
                        if (counter_offset > state.legacy_counters_end || state.legacy_counters_end - counter_offset < 8 ||
                            !read_at(input, counter_offset, header))
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        bytes_reader reader{header};
                        std::uint32_t engine_frame = 0, samples = 0;
                        reader.get(engine_frame);
                        reader.get(samples);
                        if (samples > (state.legacy_counters_end - counter_offset - 8) / sample_bytes)
                        {
                            return std::unexpected(capture_file_error::malformed);
                        }
                        counter_offset += 8;
                        if (index >= first_ordinal)
                        {
                            auto& frame = frames[static_cast<std::size_t>(index - first_ordinal)];
                            const std::uint64_t bytes = static_cast<std::uint64_t>(samples) * sizeof(profile_counter_sample);
                            if (engine_frame != frame.engine_frame || bytes > remaining)
                            {
                                return std::unexpected(engine_frame != frame.engine_frame ? capture_file_error::malformed : capture_file_error::resource_limit);
                            }
                            remaining -= bytes;
                            frame.counters.reserve(samples);
                            for (std::uint32_t sample_index = 0; sample_index < samples; ++sample_index)
                            {
                                std::array<std::byte, kCounterBytes> sample_buffer{};
                                if (!read_at(input, counter_offset + sample_index * static_cast<std::uint64_t>(sample_bytes),
                                             std::span(sample_buffer).first(sample_bytes)))
                                {
                                    return std::unexpected(capture_file_error::open_failed);
                                }
                                bytes_reader sample_reader{std::span(sample_buffer).first(sample_bytes)};
                                profile_counter_sample sample{};
                                std::uint16_t id = 0;
                                std::uint64_t bits = 0;
                                if (!sample_reader.get(id) || !sample_reader.get(bits) || id == 0 || id > known ||
                                    (state.legacy_counter_version >= 2 &&
                                     (!sample_reader.get(sample.cpu.session) || !sample_reader.get(sample.cpu.tick) || !sample_reader.get(sample.cpu.task))))
                                {
                                    return std::unexpected(capture_file_error::malformed);
                                }
                                sample.id = static_cast<profile_counter_id>(id);
                                sample.value = std::bit_cast<double>(bits);
                                if (!std::isfinite(sample.value))
                                {
                                    return std::unexpected(capture_file_error::malformed);
                                }
                                frame.counters.push_back(sample);
                            }
                        }
                        counter_offset += samples * static_cast<std::uint64_t>(sample_bytes);
                    }
                }
            }
            return std::make_shared<const capture_session>(
                std::move(frames), std::vector<thread_info>(state.metadata->threads().begin(), state.metadata->threads().end()),
                std::vector<capture_marker>(state.metadata->markers().begin(), state.metadata->markers().end()),
                state.metadata->environment(), state.complete, state.unacked,
                add(state.status.source_dropped_counters, state.status.dropped_counters),
                std::vector<capture_counter>(state.metadata->counter_descriptors().begin(), state.metadata->counter_descriptors().end()));
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        catch (...)
        {
            return std::unexpected(capture_file_error::open_failed);
        }
    }

    std::expected<void, capture_file_error> save_recording(const capture_recording& recording,
                                                          const std::filesystem::path& path, std::stop_token cancel)
    {
        using namespace detail::recording_file_impl;
        // 검증된 접두 구간만 복사한다. 아직 열린 녹화에 나중에 붙은 바이트는 포함하지 않는다.
        std::filesystem::path directory;
        std::filesystem::path temporary;
        try
        {
            std::error_code error;
            const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
            static std::atomic<std::uint64_t> next_id{0};
            for (std::uint32_t attempt = 0; attempt < 32; ++attempt)
            {
                directory = parent / (".ceprof-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                      "-" + std::to_string(next_id.fetch_add(1)));
                if (std::filesystem::create_directory(directory, error))
                {
                    temporary = directory / "capture.ceprof";
                    break;
                }
            }
            if (temporary.empty())
            {
                return std::unexpected(capture_file_error::write_failed);
            }
            auto cleanup = [&]
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                std::filesystem::remove(directory, ignored);
            };
            std::ifstream input(recording.path(), std::ios::binary);
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            std::array<std::byte, 64 * 1024> scratch{};
            std::uint64_t remaining = recording.valid_bytes();
            if (!input || !output)
            {
                output.close();
                cleanup();
                return std::unexpected(capture_file_error::open_failed);
            }
            while (remaining != 0)
            {
                if (cancel.stop_requested())
                {
                    input.close();
                    output.close();
                    cleanup();
                    return std::unexpected(capture_file_error::canceled);
                }
                const auto amount = static_cast<std::size_t>((std::min)(remaining, static_cast<std::uint64_t>(scratch.size())));
                if (!input.read(reinterpret_cast<char*>(scratch.data()), static_cast<std::streamsize>(amount)) ||
                    !write_bytes(output, std::span(scratch).first(amount)))
                {
                    output.close();
                    cleanup();
                    return std::unexpected(capture_file_error::write_failed);
                }
                remaining -= amount;
            }
            output.flush();
            output.close();
            input.close();
            // 교체 전에 복사본을 다시 검증해 복사 도중 원본이 손상되었는지 확인한다.
            const auto copied = open_capture_recording(temporary, cancel);
            if (cancel.stop_requested())
            {
                cleanup();
                return std::unexpected(capture_file_error::canceled);
            }
            if (!output || !copied || (*copied)->valid_bytes() != recording.valid_bytes() ||
                (*copied)->frame_count() != recording.frame_count() || !replace_file(temporary, path))
            {
                cleanup();
                return std::unexpected(copied ? capture_file_error::write_failed : copied.error());
            }
            cleanup();
            return {};
        }
        catch (...)
        {
            std::error_code ignored;
            if (!temporary.empty())
            {
                std::filesystem::remove(temporary, ignored);
                std::filesystem::remove(directory, ignored);
            }
            return std::unexpected(capture_file_error::write_failed);
        }
    }
}
