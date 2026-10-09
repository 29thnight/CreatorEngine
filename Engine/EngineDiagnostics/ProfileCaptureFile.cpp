#include "ProfileCaptureFile.h"

#include <array>
#include <algorithm>
#include <limits>
#include <atomic>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>

// 유니티 빌드가 켜져 있으므로 익명 네임스페이스를 쓰지 않는다 — 격리 단위가
// 파일이 아니라 blob 이라 같은 이름이 남의 청크와 충돌한다.
namespace ce::detail::capture_file_impl
{
    // ★ 정수를 memcpy 로 쓴다. 그러면 파일의 바이트 순서가 곧 이 기계의 것이
    //   되는데, 형식은 little-endian 으로 정했다. 다른 순서의 기계로 옮기는
    //   날 **조용히 다른 형식을 쓰는** 대신 여기서 컴파일이 멈추게 한다.
    static_assert(std::endian::native == std::endian::little,
                  ".ceprof 는 little-endian 이다 — 이 기계에서는 바이트를 뒤집어 써야 한다");

    constexpr std::array<char, 8> kMagic{ 'C', 'E', 'P', 'R', 'O', 'F', '\0', '\0' };
    constexpr std::size_t kHeaderBytes = 16;       // magic 8 · version 4 · chunk_count 4
    constexpr std::size_t kChunkEntryBytes = 32;   // type · version · offset · size · crc · reserved

    // 청크 표가 이보다 길면 머리가 손상된 것이다. 이 판정이 없으면 망가진
    // 개수 하나가 "잘렸다" 로 읽힌다 — 표의 크기가 파일보다 커 보이기 때문이다.
    constexpr std::uint32_t kMaxChunks = 64;

    enum class chunk_type : std::uint32_t
    {
        environment = 1,
        markers     = 2,
        threads     = 3,
        frames      = 4,
        counters    = 5,
        counter_descriptors = 6,
        render_measurements = 7,
    };
    constexpr std::uint32_t kChunkVersion = 1;

    // 레코드 하나의 최소 바이트. 개수를 믿기 전에 **남은 바이트로 그만큼이
    // 들어갈 수 있는지** 먼저 잰다 — 손상된 개수 하나로 거대한 할당을 하지 않는다.
    constexpr std::size_t kMarkerMinBytes = 1 + 4 + 4 + 4;
    constexpr std::size_t kThreadMinBytes = 4 + 4 + 1 + 4 + 4;
    constexpr std::size_t kFrameHeadBytes = 4 + 8 + 8 + 8 + 4;
    constexpr std::size_t kEventBytes = 8 + 8 + 4 + 4 + 2 + 2 + 1 + 1 + 4 + 2 + 2;

    constexpr std::uint8_t kKnownEventFlags = 0x0F;   // truncated_begin·end · gpu_span · instant

    // ── CRC-32 (IEEE 802.3, 다항식 0xEDB88320) ─────────────────────────────
    //
    // 이 라이브러리는 ProjectReference 0 이다. 스무 줄을 위해 압축 라이브러리를
    // 끌어오면 그 계약이 깨진다.
    constexpr std::array<std::uint32_t, 256> make_crc_table()
    {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
            {
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            table[i] = value;
        }
        return table;
    }
    constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

    std::uint32_t crc32(std::span<const std::byte> bytes)
    {
        std::uint32_t value = 0xFFFFFFFFu;
        for (const std::byte b : bytes)
        {
            value = kCrcTable[(value ^ static_cast<std::uint8_t>(b)) & 0xFFu] ^ (value >> 8);
        }
        return value ^ 0xFFFFFFFFu;
    }

    // ── 쓰는 쪽 ──────────────────────────────────────────────────────────────
    class byte_writer
    {
    public:
        template <typename T>
        void put(T value)
        {
            static_assert(std::is_integral_v<T>);
            const std::size_t at = m_bytes.size();
            m_bytes.resize(at + sizeof(T));
            std::memcpy(m_bytes.data() + at, &value, sizeof(T));
        }

        void put_string(const std::string& value)
        {
            put(static_cast<std::uint32_t>(value.size()));
            const std::size_t at = m_bytes.size();
            m_bytes.resize(at + value.size());
            std::memcpy(m_bytes.data() + at, value.data(), value.size());
        }

        void put_bytes(std::span<const std::byte> value)
        {
            m_bytes.insert(m_bytes.end(), value.begin(), value.end());
        }

        std::vector<std::byte> take() { return std::move(m_bytes); }
        std::size_t size() const { return m_bytes.size(); }

    private:
        std::vector<std::byte> m_bytes;
    };

    // ── 읽는 쪽 ──────────────────────────────────────────────────────────────
    //
    // ★ 던지지 않는다. 경계를 넘는 읽기는 전부 false 를 내고, 부른 쪽이 그것을
    //   오류로 바꾼다. 손상된 파일은 예외적인 일이 아니라 **예상한 입력**이다.
    class byte_reader
    {
    public:
        explicit byte_reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

        template <typename T>
        bool get(T& value)
        {
            static_assert(std::is_integral_v<T>);
            if (remaining() < sizeof(T))
            {
                return false;
            }
            std::memcpy(&value, m_bytes.data() + m_at, sizeof(T));
            m_at += sizeof(T);
            return true;
        }

        bool get_string(std::string& value)
        {
            std::uint32_t length = 0;
            if (!get(length) || remaining() < length)
            {
                return false;
            }
            value.assign(reinterpret_cast<const char*>(m_bytes.data() + m_at), length);
            m_at += length;
            return true;
        }

        std::size_t remaining() const { return m_bytes.size() - m_at; }

        bool skip(std::size_t bytes)
        {
            if (bytes > remaining())
            {
                return false;
            }
            m_at += bytes;
            return true;
        }

        // 개수를 믿어도 되는가 — 레코드가 최소 크기로만 채워져도 남은 바이트에
        // 다 들어가는가.
        bool can_hold(std::uint64_t count, std::size_t record_bytes) const
        {
            return count <= remaining() / record_bytes;
        }

    private:
        std::span<const std::byte> m_bytes;
        std::size_t                m_at = 0;
    };

    // ── 청크 몸통 ────────────────────────────────────────────────────────────
    std::vector<std::byte> encode_environment(const capture_session& capture)
    {
        byte_writer out;
        out.put(static_cast<std::uint64_t>(capture.environment().ticks_per_second));
        out.put(static_cast<std::uint8_t>(capture.complete() ? 1 : 0));
        out.put(capture.unacked_streams());
        return out.take();
    }

    std::vector<std::byte> encode_markers(const capture_session& capture)
    {
        byte_writer out;
        out.put(capture.marker_count());
        for (const capture_marker& value : capture.markers())
        {
            out.put(static_cast<std::uint8_t>(value.kind));
            out.put(value.line);
            out.put_string(value.name);
            out.put_string(value.file);
        }
        return out.take();
    }

    std::vector<std::byte> encode_threads(const capture_session& capture)
    {
        byte_writer out;
        out.put(static_cast<std::uint32_t>(capture.threads().size()));
        for (const thread_info& value : capture.threads())
        {
            out.put(value.slot);
            out.put(value.os_thread_id);
            out.put(static_cast<std::uint8_t>(value.kind));
            out.put(value.track_order);
            out.put_string(value.name);
        }
        return out.take();
    }

    // ★ 필드 하나씩 쓴다. 구조체를 통째로 복사하면 패딩과 배치가 곧 형식이 된다.
    void encode_event(byte_writer& out, const profile_event& value)
    {
        out.put(static_cast<std::uint64_t>(value.tick_begin));
        out.put(static_cast<std::uint64_t>(value.tick_end));
        out.put(static_cast<std::uint32_t>(value.marker));
        out.put(value.frame);
        out.put(value.thread_slot);
        out.put(value.depth);
        out.put(static_cast<std::uint8_t>(value.flags));
        out.put(value.queue);
        out.put(value.submission);
        out.put(value.view);
        out.put(value.reserved);
        out.put(value.cpu.session);
        out.put(value.cpu.tick);
        out.put(value.cpu.task);
    }

    std::vector<std::byte> encode_frames(const capture_session& capture)
    {
        byte_writer out;
        out.put(capture.frame_count());
        for (const frame_record& frame : capture.frames())
        {
            out.put(frame.engine_frame);
            out.put(static_cast<std::uint64_t>(frame.tick_begin));
            out.put(static_cast<std::uint64_t>(frame.tick_end));
            out.put(frame.dropped_events);
            out.put(static_cast<std::uint32_t>(frame.events.size()));
            for (const profile_event& value : frame.events)
            {
                encode_event(out, value);
            }
        }
        return out.take();
    }

    std::vector<std::byte> encode_counters(const capture_session& capture)
    {
        byte_writer out;
        out.put(capture.dropped_counters());
        out.put(capture.frame_count());
        for (const frame_record& frame : capture.frames())
        {
            out.put(frame.engine_frame);
            out.put(static_cast<std::uint32_t>(frame.counters.size()));
            for (const profile_counter_sample& sample : frame.counters)
            {
                std::uint64_t bits = 0;
                static_assert(sizeof(bits) == sizeof(sample.value));
                std::memcpy(&bits, &sample.value, sizeof(bits));
                out.put(static_cast<std::uint16_t>(sample.id));
                out.put(bits);
                out.put(sample.cpu.session);
                out.put(sample.cpu.tick);
                out.put(sample.cpu.task);
            }
        }
        return out.take();
    }

    std::vector<std::byte> encode_render_measurements(const capture_session& capture)
    {
        byte_writer out;
        out.put(capture.frame_count());
        for (const auto& frame : capture.frames())
        {
            out.put(frame.engine_frame);
            out.put(static_cast<std::uint32_t>(frame.render_measurements.size()));
            for (const auto& sample : frame.render_measurements)
            {
                encode_render_measurement(out, sample);
            }
        }
        return out.take();
    }

    std::vector<std::byte> encode_counter_descriptors(const capture_session& capture)
    {
        byte_writer out;
        out.put(static_cast<std::uint32_t>(capture.counter_descriptors().size()));
        for (const capture_counter& value : capture.counter_descriptors())
        {
            out.put(static_cast<std::uint16_t>(value.id));
            out.put(counter_bit(value.category));
            out.put_string(value.name);
            out.put_string(value.unit);
        }
        return out.take();
    }

    // ── 청크 몸통 읽기 ───────────────────────────────────────────────────────
    using parse_result = std::expected<void, capture_file_error>;

    // 청크를 **남김없이** 읽었는가. 남은 바이트가 있으면 쓰는 쪽과 읽는 쪽이
    // 필드 하나라도 어긋난 것이다 — 그 어긋남이 여기서 드러난다.
    parse_result finish(const byte_reader& in)
    {
        if (0 != in.remaining())
        {
            return std::unexpected(capture_file_error::malformed);
        }
        return {};
    }

    parse_result parse_environment(byte_reader in, capture_environment& environment,
                                   bool& complete, std::uint32_t& unacked)
    {
        std::uint64_t frequency = 0;
        std::uint8_t  completeFlag = 0;
        if (!in.get(frequency) || !in.get(completeFlag) || !in.get(unacked) || completeFlag > 1)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        environment.ticks_per_second = frequency;
        complete = (1 == completeFlag);
        return finish(in);
    }

    parse_result parse_markers(byte_reader in, std::vector<capture_marker>& markers)
    {
        std::uint32_t count = 0;
        if (!in.get(count) || !in.can_hold(count, kMarkerMinBytes))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        markers.resize(count);
        for (capture_marker& value : markers)
        {
            std::uint8_t kind = 0;
            if (!in.get(kind) || !in.get(value.line) || !in.get_string(value.name) ||
                !in.get_string(value.file) || kind > static_cast<std::uint8_t>(marker_kind::gpu_span))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            value.kind = static_cast<marker_kind>(kind);
        }
        return finish(in);
    }

    parse_result parse_threads(byte_reader in, std::vector<thread_info>& threads)
    {
        std::uint32_t count = 0;
        if (!in.get(count) || !in.can_hold(count, kThreadMinBytes))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        threads.resize(count);
        for (thread_info& value : threads)
        {
            std::uint8_t kind = 0;
            if (!in.get(value.slot) || !in.get(value.os_thread_id) || !in.get(kind) ||
                !in.get(value.track_order) || !in.get_string(value.name) ||
                kind > static_cast<std::uint8_t>(track_kind::physics_worker))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            value.kind = static_cast<track_kind>(kind);
        }
        return finish(in);
    }

    bool parse_event(byte_reader& in, profile_event& value, std::uint32_t version)
    {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
        std::uint32_t marker = 0;
        std::uint8_t  flags = 0;
        if (!in.get(begin) || !in.get(end) || !in.get(marker) || !in.get(value.frame) ||
            !in.get(value.thread_slot) || !in.get(value.depth) || !in.get(flags) ||
            !in.get(value.queue) || !in.get(value.submission) || !in.get(value.view) ||
            !in.get(value.reserved) || 0 != (flags & ~kKnownEventFlags))
        {
            return false;
        }
        if (version >= 2 && (!in.get(value.cpu.session) || !in.get(value.cpu.tick) || !in.get(value.cpu.task)))
        {
            return false;
        }

        if ((flags & static_cast<std::uint8_t>(event_flags::gpu_span)) != 0 &&
            (value.cpu.session != 0 || value.cpu.tick != 0 || value.cpu.task != 0))
        {
            return false;
        }

        value.tick_begin = begin;
        value.tick_end = end;
        value.marker = marker;
        value.flags = static_cast<event_flags>(flags);
        return true;
    }

    parse_result parse_frame(byte_reader& in, frame_record& frame, std::uint32_t version)
    {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
        std::uint32_t events = 0;
        if (!in.get(frame.engine_frame) || !in.get(begin) || !in.get(end) ||
            !in.get(frame.dropped_events) || !in.get(events) || !in.can_hold(events, kEventBytes + (version >= 2 ? 24 : 0)))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        frame.tick_begin = begin;
        frame.tick_end = end;
        frame.events.resize(events);
        for (std::uint32_t eventIndex = 0; eventIndex < events; ++eventIndex)
        {
            profile_event& value = frame.events.mutable_at(eventIndex);
            if (!parse_event(in, value, version))
            {
                return std::unexpected(capture_file_error::malformed);
            }
        }
        return {};
    }

    parse_result parse_frames(byte_reader in, std::vector<frame_record>& frames, std::uint32_t version)
    {
        std::uint32_t count = 0;
        if (!in.get(count) || !in.can_hold(count, kFrameHeadBytes))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        frames.resize(count);
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            if (const parse_result parsed = parse_frame(in, frames[i], version); !parsed)
            {
                return parsed;
            }
            // ★ 엄격히 오름차순이어야 한다. find_frame 이 이분 탐색이라, 순서가
            //   어긋난 캡처는 멈추지 않고 **엉뚱한 프레임**을 낸다.
            if (i > 0 && frames[i - 1].engine_frame >= frames[i].engine_frame)
            {
                return std::unexpected(capture_file_error::malformed);
            }
        }
        return finish(in);
    }

    parse_result parse_counters(byte_reader in, std::vector<frame_record>& frames,
                                std::uint64_t& dropped, std::uint32_t version)
    {
        std::uint32_t count = 0;
        if (!in.get(dropped) || !in.get(count) || count != frames.size() || !in.can_hold(count, 8))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        for (frame_record& frame : frames)
        {
            std::uint32_t number = 0, samples = 0;
            if (!in.get(number) || !in.get(samples) || number != frame.engine_frame ||
                !in.can_hold(samples, version >= 2 ? 34 : 10))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            frame.counters.reserve(samples);
            std::set<std::tuple<std::uint16_t, std::uint64_t, std::uint64_t, std::uint64_t>> identities;
            for (std::uint32_t i = 0; i < samples; ++i)
            {
                std::uint16_t id = 0;
                std::uint64_t bits = 0;
                if (!in.get(id) || !in.get(bits) || id == 0)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
                profile_counter_sample sample{};
                sample.id = static_cast<profile_counter_id>(id);
                std::memcpy(&sample.value, &bits, sizeof(bits));
                if (version >= 2 && (!in.get(sample.cpu.session) || !in.get(sample.cpu.tick) || !in.get(sample.cpu.task)))
                {
                    return std::unexpected(capture_file_error::malformed);
                }
                if (!std::isfinite(sample.value))
                {
                    return std::unexpected(capture_file_error::malformed);
                }
                if (!identities.emplace(id, sample.cpu.session, sample.cpu.tick, sample.cpu.task).second)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
                frame.counters.push_back(sample);
            }
        }
        return finish(in);
    }

    parse_result parse_counter_descriptors(byte_reader in, std::vector<capture_counter>& values)
    {
        std::uint32_t count = 0;
        if (!in.get(count) || count > 65535 || !in.can_hold(count, 14))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        values.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint16_t id = 0;
            counter_mask category = 0;
            capture_counter value{};
            if (!in.get(id) || !in.get(category) || !in.get_string(value.name) ||
                !in.get_string(value.unit) || id != i + 1 || value.name.empty() ||
                value.name.size() > 255 || value.unit.size() > 32 ||
                category == 0 || (category & (category - 1)) != 0 || category > counter_bit(counter_category::audio))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            value.id = static_cast<profile_counter_id>(id);
            value.category = static_cast<counter_category>(category);
            values.push_back(std::move(value));
        }
        return finish(in);
    }

    parse_result parse_render_measurements(byte_reader in, std::vector<frame_record>& frames)
    {
        std::uint32_t count = 0;
        if (!in.get(count) || count != frames.size() || !in.can_hold(count, 8))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        for (auto& frame : frames)
        {
            std::uint32_t engineFrame = 0, samples = 0;
            if (!in.get(engineFrame) || engineFrame != frame.engine_frame || !in.get(samples) ||
                !in.can_hold(samples, kRenderMeasurementWireBytes))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            frame.render_measurements.resize(samples);
            for (auto& sample : frame.render_measurements)
            {
                if (!decode_render_measurement(in, sample) || sample.engine_frame != frame.engine_frame)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
        }
        return finish(in);
    }

    // ── 머리와 청크 표 ───────────────────────────────────────────────────────
    struct chunk_entry
    {
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::uint32_t crc = 0;
    };

    std::expected<std::vector<chunk_entry>, capture_file_error>
    read_chunk_table(std::span<const std::byte> bytes)
    {
        // 매직을 볼 수 있을 만큼도 없으면 "잘렸다" 다. 매직이 다르면 캡처가 아니다.
        if (bytes.size() < kMagic.size())
        {
            return std::unexpected(capture_file_error::truncated);
        }
        if (0 != std::memcmp(bytes.data(), kMagic.data(), kMagic.size()))
        {
            return std::unexpected(capture_file_error::not_a_capture);
        }

        byte_reader in(bytes.subspan(kMagic.size()));
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (!in.get(version) || !in.get(count))
        {
            return std::unexpected(capture_file_error::truncated);
        }
        if (version != 1 && version != 2 && version != kCaptureFileVersion)
        {
            return std::unexpected(capture_file_error::unsupported_version);
        }
        if (0 == version || count > kMaxChunks)
        {
            return std::unexpected(capture_file_error::malformed);
        }

        std::vector<chunk_entry> entries(count);
        for (chunk_entry& entry : entries)
        {
            std::uint32_t reserved = 0;
            if (!in.get(entry.type) || !in.get(entry.version) || !in.get(entry.offset) ||
                !in.get(entry.size) || !in.get(entry.crc) || !in.get(reserved))
            {
                return std::unexpected(capture_file_error::truncated);
            }
        }
        return entries;
    }

    // ★ 범위를 **모든 청크에 대해 먼저** 잰다. 잘린 파일은 마지막 청크가
    //   파일 끝을 넘으므로 여기서 "잘렸다" 가 된다. CRC 를 먼저 보면 잘린
    //   파일이 "손상됐다" 로 읽혀, 다시 받으면 될 일을 버리게 만든다.
    bool all_chunks_within(std::span<const chunk_entry> entries, std::size_t file_size)
    {
        for (const chunk_entry& entry : entries)
        {
            if (entry.offset > file_size || entry.size > file_size - entry.offset)
            {
                return false;
            }
        }
        return true;
    }
}

// ★ `using namespace` 를 namespace ce 에 두지 않는다. 유니티 빌드에서 이 파일
//   뒤에 붙는 파일의 namespace ce 로 그대로 새어, 남의 `finish`·`crc32` 와
//   겹친다. 쓰는 함수 안에만 둔다.
namespace ce
{
    const char* describe(capture_file_error error)
    {
        switch (error)
        {
        case capture_file_error::open_failed:         return "파일을 열 수 없다";
        case capture_file_error::write_failed:        return "쓰다가 실패했다 — 기존 파일은 그대로다";
        case capture_file_error::not_a_capture:       return ".ceprof 캡처 파일이 아니다";
        case capture_file_error::unsupported_version: return "이 빌드보다 새 형식이다 — 새 빌드로 열어야 한다";
        case capture_file_error::truncated:           return "파일이 잘렸다 — 쓰다 끊긴 파일이다";
        case capture_file_error::checksum_mismatch:   return "파일이 손상됐다 — 체크섬이 맞지 않는다";
        case capture_file_error::resource_limit:     return "캡처 범위가 메모리 한도를 넘는다 — 더 작은 구간을 선택해야 한다";
        case capture_file_error::malformed:           return "파일의 구조가 앞뒤가 맞지 않는다";
        case capture_file_error::canceled:            return "Capture operation canceled";
        }
        return "알 수 없는 오류";
    }

    std::vector<std::byte> encode_capture(const capture_session& capture)
    {
        using namespace ce::detail::capture_file_impl;
        const std::array<std::pair<chunk_type, std::vector<std::byte>>, 7> chunks{ {
            { chunk_type::environment, encode_environment(capture) },
            { chunk_type::markers,     encode_markers(capture) },
            { chunk_type::threads,     encode_threads(capture) },
            { chunk_type::frames,      encode_frames(capture) },
            { chunk_type::counters,    encode_counters(capture) },
            { chunk_type::counter_descriptors, encode_counter_descriptors(capture) },
            { chunk_type::render_measurements, encode_render_measurements(capture) },
        } };

        byte_writer out;
        out.put_bytes(std::as_bytes(std::span(kMagic)));
        out.put(kCaptureFileVersion);
        out.put(static_cast<std::uint32_t>(chunks.size()));

        std::uint64_t offset = kHeaderBytes + chunks.size() * kChunkEntryBytes;
        for (const auto& [type, body] : chunks)
        {
            out.put(static_cast<std::uint32_t>(type));
            out.put((type == chunk_type::frames || type == chunk_type::threads || type == chunk_type::counters) ? std::uint32_t{2} : kChunkVersion);
            out.put(offset);
            out.put(static_cast<std::uint64_t>(body.size()));
            out.put(crc32(body));
            out.put(std::uint32_t{ 0 });
            offset += body.size();
        }
        for (const auto& chunk : chunks)
        {
            out.put_bytes(chunk.second);
        }
        return out.take();
    }

    std::expected<std::vector<std::byte>, capture_file_error>
    encode_capture_bounded(const capture_session& capture, std::size_t max_wire_bytes)
    {
        // Fixed cardinalities also bound decoder work (not only wire bytes).
        constexpr std::size_t maximum = 128 * 1024 * 1024;
        if (max_wire_bytes == 0 || max_wire_bytes > maximum || capture.frames().size() > 4096 ||
            capture.markers().size() > 65535 || capture.threads().size() > 65535 ||
            capture.counter_descriptors().size() > 65535 || capture.total_events() > 2097152)
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        std::size_t size = 16 + 7 * 32 + 13 + 4 + 4 + 4 + 12 + 4 + 4;
        const auto add = [&](std::size_t count)
        {
            if (size > max_wire_bytes || count > max_wire_bytes - size)
            {
                return false;
            }
            size += count;
            return true;
        };
        const auto text_fits = [](const std::string& value) { return value.size() <= 1024; };
        for (const auto& marker : capture.markers())
        {
            if (!text_fits(marker.name) || !text_fits(marker.file) ||
                !add(13 + marker.name.size() + marker.file.size()))
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
        }
        for (const auto& thread : capture.threads())
        {
            if (!text_fits(thread.name) || !add(17 + thread.name.size()))
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
        }
        std::size_t counter_count = 0;
        std::size_t measurement_count = 0;
        for (const auto& frame : capture.frames())
        {
            if (frame.counters.size() > 4096 || frame.events.size() > 2097152 ||
                frame.render_measurements.size() > 65536 ||
                !add(48 + frame.events.size() * 62 + frame.counters.size() * 34 +
                    frame.render_measurements.size() * kRenderMeasurementWireBytes))
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
            counter_count += frame.counters.size();
            measurement_count += frame.render_measurements.size();
            if (counter_count > 1048576 || measurement_count > 1048576)
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
            for (const auto& sample : frame.render_measurements)
            {
                if (!sample.valid() || sample.engine_frame != frame.engine_frame)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
        }
        for (const auto& counter : capture.counter_descriptors())
        {
            if (!text_fits(counter.name) || !text_fits(counter.unit) ||
                !add(14 + counter.name.size() + counter.unit.size()))
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
        }
        if (size > max_wire_bytes)
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        auto encoded = encode_capture(capture);
        if (encoded.size() != size)
        {
            return std::unexpected(capture_file_error::malformed);
        }
        return encoded;
    }

    std::expected<capture_session_ptr, capture_file_error>
    decode_capture_bounded(std::span<const std::byte> bytes, std::size_t max_wire_bytes)
    {
        using namespace ce::detail::capture_file_impl;
        if (max_wire_bytes == 0 || max_wire_bytes > 128 * 1024 * 1024 || bytes.size() > max_wire_bytes)
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        const auto table = read_chunk_table(bytes);
        if (!table || !all_chunks_within(*table, bytes.size()))
        {
            return std::unexpected(table ? capture_file_error::truncated : table.error());
        }
        std::uint32_t seen = 0;
        std::uint64_t events = 0;
        std::uint64_t counters = 0;
        std::uint64_t measurements = 0;
        for (const auto& entry : *table)
        {
            if (entry.type < 1 || entry.type > 7 || (seen & (1u << entry.type)) != 0)
            {
                return std::unexpected(capture_file_error::malformed);
            }
            seen |= 1u << entry.type;
            byte_reader in(bytes.subspan(static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size)));
            const auto string_fits = [&]
            {
                std::uint32_t size = 0;
                return in.get(size) && size <= 1024 && in.skip(size);
            };
            if (entry.type == 1)
            {
                continue;
            }
            if (entry.type == 5 && !in.skip(8))
            {
                return std::unexpected(capture_file_error::malformed);
            }
            std::uint32_t count = 0;
            if (!in.get(count) || count > ((entry.type == 4 || entry.type == 5 || entry.type == 7) ? 4096u : 65535u))
            {
                return std::unexpected(capture_file_error::resource_limit);
            }
            for (std::uint32_t index = 0; index < count; ++index)
            {
                bool valid = false;
                if (entry.type == 2)
                {
                    valid = in.skip(5) && string_fits() && string_fits();
                }
                else if (entry.type == 3)
                {
                    valid = in.skip(13) && string_fits();
                }
                else if (entry.type == 6)
                {
                    valid = in.skip(6) && string_fits() && string_fits();
                }
                else if (entry.type == 7)
                {
                    std::uint32_t samples = 0;
                    valid = in.skip(4) && in.get(samples);
                    measurements += samples;
                    valid = valid && samples <= 65536 && measurements <= 1048576 &&
                        in.skip(static_cast<std::size_t>(samples) * kRenderMeasurementWireBytes);
                }
                else
                {
                    std::uint32_t items = 0;
                    const bool frame = entry.type == 4;
                    valid = in.skip(frame ? 28 : 4) && in.get(items);
                    if (frame)
                    {
                        events += items;
                        valid = valid && events <= 2097152 && in.skip(static_cast<std::size_t>(items) *
                                                                 (entry.version >= 2 ? 62 : 38));
                    }
                    else
                    {
                        counters += items;
                        valid = valid && items <= 4096 && counters <= 1048576 &&
                                in.skip(static_cast<std::size_t>(items) * (entry.version >= 2 ? 34 : 10));
                    }
                }
                if (!valid)
                {
                    return std::unexpected(capture_file_error::resource_limit);
                }
            }
            if (in.remaining() != 0)
            {
                return std::unexpected(capture_file_error::malformed);
            }
        }
        return decode_capture(bytes);
    }

    std::expected<capture_session_ptr, capture_file_error>
    decode_capture(std::span<const std::byte> bytes)
    {
        using namespace ce::detail::capture_file_impl;
        const auto table = read_chunk_table(bytes);
        if (!table)
        {
            return std::unexpected(table.error());
        }
        if (!all_chunks_within(*table, bytes.size()))
        {
            return std::unexpected(capture_file_error::truncated);
        }

        std::vector<frame_record>   frames;
        std::vector<thread_info>    threads;
        std::vector<capture_marker> markers;
        std::vector<capture_counter> counterDescriptors;
        capture_environment         environment{};
        bool                        complete = true;
        std::uint32_t               unacked = 0;
        std::uint64_t               droppedCounters = 0;
        std::uint32_t               seen = 0;   // 필수 청크 넷의 비트
        std::span<const std::byte> countersBody{};
        std::uint32_t countersVersion = 1;
        std::span<const std::byte> measurementsBody{};
        std::uint32_t formatVersion = 0;
        byte_reader header(bytes.subspan(kMagic.size()));
        header.get(formatVersion);

        for (const chunk_entry& entry : *table)
        {
            const std::span<const std::byte> body = bytes.subspan(
                static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size));
            if (crc32(body) != entry.crc)
            {
                return std::unexpected(capture_file_error::checksum_mismatch);
            }
            if (entry.version == 0 || entry.version > ((entry.type == static_cast<std::uint32_t>(chunk_type::frames) || entry.type == static_cast<std::uint32_t>(chunk_type::threads) || entry.type == static_cast<std::uint32_t>(chunk_type::counters)) ? 2u : kChunkVersion))
            {
                return std::unexpected(capture_file_error::unsupported_version);
            }

            parse_result parsed{};
            const std::uint32_t bit = (entry.type >= 1 && entry.type <= 7) ? (1u << entry.type) : 0u;
            if (0 != (seen & bit))
            {
                return std::unexpected(capture_file_error::malformed);   // 같은 청크가 둘
            }
            seen |= bit;

            switch (static_cast<chunk_type>(entry.type))
            {
            case chunk_type::environment:
                parsed = parse_environment(byte_reader(body), environment, complete, unacked);
                break;
            case chunk_type::markers: parsed = parse_markers(byte_reader(body), markers); break;
            case chunk_type::threads: parsed = parse_threads(byte_reader(body), threads); break;
            case chunk_type::frames:  parsed = parse_frames(byte_reader(body), frames, entry.version); break;
            case chunk_type::counters: countersBody = body; countersVersion = entry.version; break;
            case chunk_type::counter_descriptors:
                parsed = parse_counter_descriptors(byte_reader(body), counterDescriptors); break;
            case chunk_type::render_measurements: measurementsBody = body; break;
            default:
                // 모르는 청크는 건너뛴다 — 같은 형식 버전 안에서 덧붙인 것이다.
                // CRC 는 이미 봤으므로 손상은 아니다.
                break;
            }
            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }
        }

        constexpr std::uint32_t kRequired = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
        if (kRequired != (seen & kRequired))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        if ((formatVersion == kCaptureFileVersion) != ((seen & (1u << 7)) != 0))
        {
            return std::unexpected(capture_file_error::malformed);
        }
        if ((seen & (1u << 7)) != 0)
        {
            if (const auto parsed = parse_render_measurements(byte_reader(measurementsBody), frames); !parsed)
            {
                return std::unexpected(parsed.error());
            }
        }
        if (!countersBody.empty())
        {
            if (const parse_result parsed = parse_counters(byte_reader(countersBody), frames,
                                                            droppedCounters, countersVersion); !parsed)
            {
                return std::unexpected(parsed.error());
            }
        }
        const std::size_t knownCounters = counterDescriptors.empty() ? 5 : counterDescriptors.size();
        for (const frame_record& frame : frames)
        {
            for (const auto& sample : frame.render_measurements)
            {
                if (sample.axis == profile_render_axis::gpu_pass && sample.marker >= markers.size())
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
            for (const profile_counter_sample& sample : frame.counters)
            {
                if (static_cast<std::uint16_t>(sample.id) > knownCounters)
                {
                    return std::unexpected(capture_file_error::malformed);
                }
            }
        }

        return std::make_shared<const capture_session>(
            std::move(frames), std::move(threads), std::move(markers),
            environment, complete, unacked, droppedCounters,
            std::move(counterDescriptors));
    }

    std::expected<void, capture_file_error>
    save_capture(const capture_session& capture, const std::filesystem::path& path)
    {
        std::filesystem::path directory;
        std::filesystem::path temporary;
        try
        {
            const std::vector<std::byte> bytes = encode_capture(capture);
            std::error_code error;
            const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
            static std::atomic<std::uint64_t> next_id{0};
            for (std::uint32_t attempt = 0; attempt < 32; ++attempt)
            {
                directory = parent / (".ceprof-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
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
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                                         static_cast<std::streamsize>(bytes.size())))
            {
                output.close();
                cleanup();
                return std::unexpected(capture_file_error::write_failed);
            }
            output.close();
            if (!output)
            {
                cleanup();
                return std::unexpected(capture_file_error::write_failed);
            }
            const auto recording = open_capture_recording(temporary);
            if (!recording)
            {
                cleanup();
                return std::unexpected(recording.error());
            }
            const auto result = save_recording(**recording, path);
            cleanup();
            return result;
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

    std::expected<capture_session_ptr, capture_file_error>
    load_capture(const std::filesystem::path& path)
    {
        const auto recording = open_capture_recording(path);
        if (!recording)
        {
            return std::unexpected(recording.error());
        }
        std::ifstream input(path, std::ios::binary);
        std::array<std::byte, 12> header{};
        if (!input.read(reinterpret_cast<char*>(header.data()), header.size()))
        {
            return std::unexpected(capture_file_error::open_failed);
        }
        std::uint32_t version = 0;
        std::memcpy(&version, header.data() + 8, sizeof(version));
        const auto count = (*recording)->frame_count();
        if (version == 3 || version == kRecordingFileVersion)
        {
            const auto selected = static_cast<std::uint32_t>((std::min)(count, std::uint64_t{kDefaultRetainedFrames}));
            return (*recording)->load_range(count - selected, selected);
        }
        // 예전 API는 v1/v2 전체 캡처를 돌려주되, 큰 파일은 범위 읽기를 요구한다.
        if (count > (std::numeric_limits<std::uint32_t>::max)())
        {
            return std::unexpected(capture_file_error::resource_limit);
        }
        return (*recording)->load_range(0, static_cast<std::uint32_t>(count));
    }
}
