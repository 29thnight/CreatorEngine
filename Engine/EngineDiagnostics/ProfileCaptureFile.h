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
// 형식(모든 정수는 little-endian):
//
//   header      magic "CEPROF\0\0" · u32 format_version · u32 chunk_count
//   chunk table chunk_count × { u32 type · u32 version · u64 offset · u64 size ·
//                               u32 crc32 · u32 reserved }
//   chunks      environment · markers · threads · frames
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
#include <vector>

#include "ProfileCapture.h"

namespace ce
{
	// 이 빌드가 쓰는 형식. 읽을 때 이보다 **새** 파일은 거절한다 — 모르는
	// 뜻을 아는 척 읽는 것보다 "읽을 수 없다" 가 낫다(§8.1).
	inline constexpr std::uint32_t kCaptureFileVersion = 1;

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
		malformed,            // 크기·개수·순서가 앞뒤가 안 맞는다
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
}
