#pragma once
// LC4 (PHASE 14.5) — 소켓 플랫폼 이음매.
//
// ── 이 파일이 존재하는 이유 (§4.1) ──────────────────────────────────────
//
// Winsock2 는 Windows 전용이고, 엔진은 이미 Win32 에 묶여 있다(Windows.h 를
// 무는 파일 32 개, 플랫폼 분기 0 개). 그래서 이식성 자체가 이 이음매의 목적은
// 아니다. 목적은 셋이다.
//
//   1. `Engine/CommandService` 는 Editor 와 Player 가 **공유**한다(§12).
//      LC8 이 구성을 가를 때 이 모듈이 양쪽에 들어간다.
//   2. §12 의 규칙 — CommandService 는 Editor 헤더를 include 하지 않는다.
//      같은 규율이 플랫폼 헤더에도 적용돼야 한다.
//   3. **`<winsock2.h>` 는 `<windows.h>` 보다 먼저 와야 한다.** 순서가 뒤집히면
//      windows.h 가 끌고 오는 WinSock 1.1 선언과 충돌해 재정의 오류가 난다.
//      이것을 공개 헤더에 두면 CommandService 를 include 하는 **모든 TU** 가
//      그 지뢰를 물려받는다. 이 헤더에는 플랫폼 헤더가 하나도 없다.
//
// 이식 시 남는 델타는 `WSAStartup`/`WSACleanup` · `SOCKET` 대 `int` ·
// `closesocket` 대 `close` · `WSAGetLastError` 대 `errno` 정도다.

#include <cstddef>
#include <cstdint>
#include <string>

namespace CommandService
{
    /// 플랫폼 소켓 핸들의 불투명 표현.
    ///
    /// Win32 의 `SOCKET` 은 `UINT_PTR` 이고 POSIX 는 `int` 다. 둘 다 담을 수
    /// 있게 `uintptr_t` 로 두고, 무효값은 플랫폼 구현이 정한다.
    using SocketHandle = std::uintptr_t;

    SocketHandle InvalidSocket() noexcept;
    bool         IsValid(SocketHandle socket) noexcept;

    /// 프로세스당 한 번. Win32 의 `WSAStartup` 이 여기 산다.
    /// POSIX 에서는 아무것도 하지 않는다.
    struct SocketSubsystem
    {
        SocketSubsystem();
        ~SocketSubsystem();

        SocketSubsystem(const SocketSubsystem&)            = delete;
        SocketSubsystem& operator=(const SocketSubsystem&) = delete;

        bool        ok{ false };
        std::string error;
    };

    /// loopback 에만 bind 한다.
    ///
    /// ★ 주소를 인자로 받지 않는다. 받으면 언젠가 `0.0.0.0` 이 들어온다 —
    ///   서비스는 실행 표면이고(§8), bind 주소는 설정이 아니라 상수여야 한다.
    ///   정적 게이트가 이 파일 밖에서 bind 호출이 없음을 확인한다.
    ///
    /// `port` 가 0 이면 OS 가 고른다. 실제 배정 포트는 `outPort` 로 돌려준다
    /// (에디터 다중 실행을 위해 고정 포트를 강제하지 않는다 — §5.1).
    SocketHandle ListenLoopback(uint16_t port, int backlog,
                                uint16_t& outPort, std::string& outError);

    /// 접속 하나를 받는다. `timeoutMs` 안에 없으면 `InvalidSocket()` 을 돌려주고
    /// `outTimedOut` 이 참이 된다(종료 요청을 확인할 수 있게 하기 위해서다 —
    /// 무한 대기하는 accept 는 스레드를 회수할 수 없다).
    SocketHandle AcceptWithTimeout(SocketHandle listener, int timeoutMs,
                                   bool& outTimedOut, std::string& outError);

    /// 최대 `size` 바이트를 읽는다. 0 은 상대가 닫았다는 뜻이고, 음수는 오류다.
    /// `timeoutMs` 를 넘기면 -2 를 돌려준다(유휴 연결 정리용 — §8 의 30초).
    int Receive(SocketHandle socket, char* buffer, std::size_t size, int timeoutMs);

    /// 전부 보낼 때까지 반복한다. 실패하면 false.
    bool SendAll(SocketHandle socket, const char* data, std::size_t size);

    void CloseSocket(SocketHandle socket) noexcept;

    /// 읽기/쓰기를 끊어 블로킹 중인 recv 를 즉시 깨운다. 핸들은 닫지 않는다.
    ///
    /// ★ 종료 때 필요하다. 작업 스레드가 유휴 타임아웃(30초) 안에서 recv 로
    ///   막혀 있으면 에디터 종료가 그만큼 늦어진다. 소켓을 닫아 버리면 그
    ///   스레드가 이미 닫힌 핸들을 다시 닫게 되므로, 깨우기만 하고 닫는 것은
    ///   소유자(작업 스레드)에게 맡긴다.
    void ShutdownSocket(SocketHandle socket) noexcept;

    /// 접속한 상대가 loopback 인가.
    ///
    /// bind 가 loopback 이면 원리적으로 참이지만, 그 전제가 깨졌을 때 조용히
    /// 열려 있는 것보다 여기서 한 번 더 막는 편이 낫다 — 이중 방어는 이 표면에서
    /// 값이 싸다.
    bool IsLoopbackPeer(SocketHandle socket) noexcept;

    /// 감사 로그에 남길 상대 포트. 주소는 loopback 뿐이라 의미가 없다.
    uint16_t PeerPort(SocketHandle socket) noexcept;

    /// CSPRNG 바이트. 토큰 생성에 쓴다(§8 — 프로세스마다 32바이트).
    bool FillRandomBytes(unsigned char* buffer, std::size_t size);
}
