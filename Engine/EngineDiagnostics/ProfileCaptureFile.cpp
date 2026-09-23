#include "ProfileCaptureFile.h"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
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
			    kind > static_cast<std::uint8_t>(track_kind::other))
			{
				return std::unexpected(capture_file_error::malformed);
			}
			value.kind = static_cast<track_kind>(kind);
		}
		return finish(in);
	}

	bool parse_event(byte_reader& in, profile_event& value)
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
		value.tick_begin = begin;
		value.tick_end = end;
		value.marker = marker;
		value.flags = static_cast<event_flags>(flags);
		return true;
	}

	parse_result parse_frame(byte_reader& in, frame_record& frame)
	{
		std::uint64_t begin = 0;
		std::uint64_t end = 0;
		std::uint32_t events = 0;
		if (!in.get(frame.engine_frame) || !in.get(begin) || !in.get(end) ||
		    !in.get(frame.dropped_events) || !in.get(events) || !in.can_hold(events, kEventBytes))
		{
			return std::unexpected(capture_file_error::malformed);
		}
		frame.tick_begin = begin;
		frame.tick_end = end;
		frame.events.resize(events);
		for (profile_event& value : frame.events)
		{
			if (!parse_event(in, value))
			{
				return std::unexpected(capture_file_error::malformed);
			}
		}
		return {};
	}

	parse_result parse_frames(byte_reader in, std::vector<frame_record>& frames)
	{
		std::uint32_t count = 0;
		if (!in.get(count) || !in.can_hold(count, kFrameHeadBytes))
		{
			return std::unexpected(capture_file_error::malformed);
		}
		frames.resize(count);
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			if (const parse_result parsed = parse_frame(in, frames[i]); !parsed)
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
		if (version > kCaptureFileVersion)
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
		case capture_file_error::malformed:           return "파일의 구조가 앞뒤가 맞지 않는다";
		}
		return "알 수 없는 오류";
	}

	std::vector<std::byte> encode_capture(const capture_session& capture)
	{
		using namespace ce::detail::capture_file_impl;
		const std::array<std::pair<chunk_type, std::vector<std::byte>>, 4> chunks{ {
			{ chunk_type::environment, encode_environment(capture) },
			{ chunk_type::markers,     encode_markers(capture) },
			{ chunk_type::threads,     encode_threads(capture) },
			{ chunk_type::frames,      encode_frames(capture) },
		} };

		byte_writer out;
		out.put_bytes(std::as_bytes(std::span(kMagic)));
		out.put(kCaptureFileVersion);
		out.put(static_cast<std::uint32_t>(chunks.size()));

		std::uint64_t offset = kHeaderBytes + chunks.size() * kChunkEntryBytes;
		for (const auto& [type, body] : chunks)
		{
			out.put(static_cast<std::uint32_t>(type));
			out.put(kChunkVersion);
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
		capture_environment         environment{};
		bool                        complete = true;
		std::uint32_t               unacked = 0;
		std::uint32_t               seen = 0;   // 필수 청크 넷의 비트

		for (const chunk_entry& entry : *table)
		{
			const std::span<const std::byte> body = bytes.subspan(
				static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size));
			if (crc32(body) != entry.crc)
			{
				return std::unexpected(capture_file_error::checksum_mismatch);
			}
			if (entry.version > kChunkVersion)
			{
				return std::unexpected(capture_file_error::unsupported_version);
			}

			parse_result parsed{};
			const std::uint32_t bit = (entry.type >= 1 && entry.type <= 4) ? (1u << entry.type) : 0u;
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
			case chunk_type::frames:  parsed = parse_frames(byte_reader(body), frames); break;
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

		return std::make_shared<const capture_session>(
			std::move(frames), std::move(threads), std::move(markers),
			environment, complete, unacked);
	}

	std::expected<void, capture_file_error>
	save_capture(const capture_session& capture, const std::filesystem::path& path)
	{
		const std::vector<std::byte> bytes = encode_capture(capture);

		// ★ 임시 파일에 **완성한 뒤** 교체한다. 같은 이름에 곧장 쓰면, 쓰다가
		//   끊긴 순간 기존 파일도 새 파일도 없는 상태가 된다.
		std::filesystem::path temporary = path;
		temporary += ".tmp";
		{
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				return std::unexpected(capture_file_error::write_failed);
			}
			out.write(reinterpret_cast<const char*>(bytes.data()),
			          static_cast<std::streamsize>(bytes.size()));
			out.close();
			if (!out)
			{
				std::error_code ignored;
				std::filesystem::remove(temporary, ignored);
				return std::unexpected(capture_file_error::write_failed);
			}
		}

		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error)
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			return std::unexpected(capture_file_error::write_failed);
		}
		return {};
	}

	std::expected<capture_session_ptr, capture_file_error>
	load_capture(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in)
		{
			return std::unexpected(capture_file_error::open_failed);
		}
		const std::streamoff size = in.tellg();
		if (size < 0)
		{
			return std::unexpected(capture_file_error::open_failed);
		}
		std::vector<std::byte> bytes(static_cast<std::size_t>(size));
		in.seekg(0);
		if (!in.read(reinterpret_cast<char*>(bytes.data()), size))
		{
			return std::unexpected(capture_file_error::open_failed);
		}
		return decode_capture(bytes);
	}
}
