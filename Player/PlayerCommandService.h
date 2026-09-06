#pragma once
// PHASE 14.5 LC8 — Player 와 CommandService 를 잇는 어댑터.
//
// Editor 의 `EditorCommandServiceHost` 와 같은 자리이고 같은 인터페이스
// (`CommandService::ICommandGateway`)에 다른 어댑터를 끼운다. §12 가 요구한
// 방향이 여기서 값을 치른다 — 서비스는 Editor 도 Player 도 모르고, 두 호스트가
// 각자 자기 어댑터를 준다.
//
// ── Shipping 에서는 본문이 없다 ─────────────────────────────────────────
//
// 선언은 남고 정의가 사라진다. `Start` 는 false 와 사유를 돌려주고, 나머지는
// 아무 일도 하지 않는다. 그래서 호출부(`PlayerApp`)는 `#if` 없이 그대로 쓴다 —
// 호출부에 조건부 컴파일을 흩으면 Shipping 빌드가 깨지는 자리가 파일 수만큼 는다.
//
// ★ **본문을 비우는 것은 격리가 아니다.** `CommandService.lib` 이 링크에서
//   빠지는 것이 격리이고, 그것은 `Player.vcxproj` 의 구성 조건부
//   `ProjectReference` 가 한다. 이 파일의 `#if` 는 그 lib 이 없을 때 **컴파일이
//   되게** 하는 것이지 심볼을 없애는 장치가 아니다.

#include <cstdint>
#include <string>

namespace PlayerCommandService
{
	/// `--command-service` 로 켠다. **기본은 off** 다(§8 · §11.2).
	///
	/// 실패해도 게임은 계속 돈다 — 서비스가 안 열린 것과 게임이 못 뜨는 것은
	/// 다른 사건이고, 후자로 만들 이유가 없다.
	///
	/// Shipping 에서는 항상 false 를 돌려준다(플래그가 있어도 없다 · §11.2).
	bool Start(const std::string& projectRoot, std::string& outError);

	void Stop() noexcept;

	bool     IsRunning() noexcept;
	uint16_t Port() noexcept;

	/// 이 빌드에 서비스가 **컴파일되어 있는가.** 진단·게이트용이다.
	///
	/// Shipping 은 false 다. 스모크가 이 값을 찍어 두면 "서비스가 안 켜졌다" 와
	/// "이 빌드에는 서비스가 없다" 가 로그에서 갈린다.
	bool IsCompiledIn() noexcept;
}
