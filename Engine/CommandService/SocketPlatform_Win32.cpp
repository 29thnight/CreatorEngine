// LC4 (PHASE 14.5) — 소켓 이음매의 Win32 구현.
//
// ★ **이 파일이 저장소에서 `<winsock2.h>` 를 무는 유일한 곳이다.**
//   그 성질을 정적 게이트가 지킨다(verify-cli-service.ps1). 공개 헤더로 새면
//   CommandService 를 include 하는 모든 TU 가 winsock2/windows 순서 지뢰를
//   물려받는다.

// winsock2.h 를 windows.h 보다 먼저 둔다. 반대로 두면 windows.h 가 끌고 오는
// WinSock 1.1 선언과 충돌한다. WIN32_LEAN_AND_MEAN 이 그 include 를 막아 주지만,
// 다른 헤더가 windows.h 를 먼저 물고 들어올 수 있으므로 순서에 기대지 않고
// 둘 다 한다.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bcrypt.lib")

#include "SocketPlatform.h"

namespace CommandService
{
    namespace
    {
        SOCKET AsSocket(SocketHandle handle) noexcept
        {
            return static_cast<SOCKET>(handle);
        }

        std::string LastErrorText(const char* what)
        {
            return std::string(what) + " 실패 (WSA " + std::to_string(::WSAGetLastError()) + ")";
        }
    }

    SocketHandle InvalidSocket() noexcept
    {
        return static_cast<SocketHandle>(INVALID_SOCKET);
    }

    bool IsValid(SocketHandle socket) noexcept
    {
        return AsSocket(socket) != INVALID_SOCKET;
    }

    SocketSubsystem::SocketSubsystem()
    {
        WSADATA data{};
        const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (0 != result)
        {
            error = "WSAStartup 실패 (" + std::to_string(result) + ")";
            return;
        }
        ok = true;
    }

    SocketSubsystem::~SocketSubsystem()
    {
        if (ok) ::WSACleanup();
    }

    SocketHandle ListenLoopback(uint16_t port, int backlog,
                                uint16_t& outPort, std::string& outError)
    {
        outPort = 0;

        const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (INVALID_SOCKET == listener)
        {
            outError = LastErrorText("socket");
            return InvalidSocket();
        }

        // ★ 주소는 상수다. INADDR_LOOPBACK 외의 값이 이 파일에 나타나면 안 된다.
        //   정적 게이트가 `INADDR_ANY`·`0.0.0.0` 을 금지한다(§14.1).
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port        = ::htons(port);

        if (SOCKET_ERROR == ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
        {
            outError = LastErrorText("bind");
            ::closesocket(listener);
            return InvalidSocket();
        }

        if (SOCKET_ERROR == ::listen(listener, backlog))
        {
            outError = LastErrorText("listen");
            ::closesocket(listener);
            return InvalidSocket();
        }

        // OS 가 고른 포트를 되읽는다(port 0 요청).
        sockaddr_in bound{};
        int boundSize = static_cast<int>(sizeof(bound));
        if (SOCKET_ERROR == ::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundSize))
        {
            outError = LastErrorText("getsockname");
            ::closesocket(listener);
            return InvalidSocket();
        }
        outPort = ::ntohs(bound.sin_port);

        return static_cast<SocketHandle>(listener);
    }

    SocketHandle AcceptWithTimeout(SocketHandle listener, int timeoutMs,
                                   bool& outTimedOut, std::string& outError)
    {
        outTimedOut = false;

        // 무한 대기하는 accept 는 스레드를 회수할 수 없다 — 종료 요청을 확인할
        // 틈을 만들려고 기한을 둔다.
        WSAPOLLFD poll{};
        poll.fd     = AsSocket(listener);
        poll.events = POLLRDNORM;

        const int ready = ::WSAPoll(&poll, 1, timeoutMs);
        if (0 == ready) { outTimedOut = true; return InvalidSocket(); }
        if (SOCKET_ERROR == ready)
        {
            outError = LastErrorText("WSAPoll");
            return InvalidSocket();
        }

        const SOCKET client = ::accept(AsSocket(listener), nullptr, nullptr);
        if (INVALID_SOCKET == client)
        {
            outError = LastErrorText("accept");
            return InvalidSocket();
        }
        return static_cast<SocketHandle>(client);
    }

    int Receive(SocketHandle socket, char* buffer, std::size_t size, int timeoutMs)
    {
        WSAPOLLFD poll{};
        poll.fd     = AsSocket(socket);
        poll.events = POLLRDNORM;

        const int ready = ::WSAPoll(&poll, 1, timeoutMs);
        if (0 == ready)             return -2;   // 유휴 타임아웃
        if (SOCKET_ERROR == ready)  return -1;

        return ::recv(AsSocket(socket), buffer, static_cast<int>(size), 0);
    }

    bool SendAll(SocketHandle socket, const char* data, std::size_t size)
    {
        std::size_t sent = 0;
        while (sent < size)
        {
            const int wrote = ::send(AsSocket(socket), data + sent,
                                     static_cast<int>(size - sent), 0);
            if (SOCKET_ERROR == wrote || 0 == wrote) return false;
            sent += static_cast<std::size_t>(wrote);
        }
        return true;
    }

    void CloseSocket(SocketHandle socket) noexcept
    {
        if (IsValid(socket)) ::closesocket(AsSocket(socket));
    }

    void ShutdownSocket(SocketHandle socket) noexcept
    {
        if (IsValid(socket)) ::shutdown(AsSocket(socket), SD_BOTH);
    }

    bool IsLoopbackPeer(SocketHandle socket) noexcept
    {
        sockaddr_in peer{};
        int size = static_cast<int>(sizeof(peer));
        if (SOCKET_ERROR == ::getpeername(AsSocket(socket),
                                          reinterpret_cast<sockaddr*>(&peer), &size))
        {
            // 상대를 확인할 수 없으면 loopback 이라고 믿지 않는다.
            return false;
        }
        if (AF_INET != peer.sin_family) return false;
        return ::ntohl(peer.sin_addr.s_addr) == INADDR_LOOPBACK;
    }

    uint16_t PeerPort(SocketHandle socket) noexcept
    {
        sockaddr_in peer{};
        int size = static_cast<int>(sizeof(peer));
        if (SOCKET_ERROR == ::getpeername(AsSocket(socket),
                                          reinterpret_cast<sockaddr*>(&peer), &size))
        {
            return 0;
        }
        return ::ntohs(peer.sin_port);
    }

    bool FillRandomBytes(unsigned char* buffer, std::size_t size)
    {
        // `rand()` 나 `std::mt19937` 를 쓰지 않는다. 이 바이트가 토큰이 되고,
        // 토큰이 실행 표면의 유일한 자물쇠다(§8).
        const NTSTATUS status = ::BCryptGenRandom(
            nullptr, buffer, static_cast<ULONG>(size),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return 0 == status;
    }
}
