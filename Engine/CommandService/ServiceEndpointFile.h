#pragma once
// LC4 (PHASE 14.5) — endpoint.json 발행과 회수 (§5.1).
//
// 클라이언트는 포트를 모른다(OS 가 고른다 — 에디터 다중 실행 때문에 고정 포트를
// 강제하지 않는다). 토큰도 프로세스마다 새로 만든다. 둘을 프로젝트 아래
// `Library/CommandService/endpoint.json` 에 적어 클라이언트가 읽게 한다.
//
// ★ **크래시 뒤의 유령 endpoint 가 가장 위험한 상태다.**
//   파일이 남아 있고 포트가 재사용되면, 클라이언트가 **다른 프로세스**에
//   명령을 보낸다. 그래서 시작할 때 남은 파일의 `pid` 가 살아 있는지 보고
//   죽었으면 회수한다.

#include <cstdint>
#include <string>

namespace CommandService
{
    struct EndpointInfo
    {
        int         schemaVersion{ 1 };
        uint32_t    pid{ 0 };
        uint16_t    port{ 0 };
        std::string token;        ///< base64url(32바이트 CSPRNG)
        std::string host{ "127.0.0.1" };
        std::string project;
        std::string role{ "editor" };
        std::string startedUtc;
    };

    /// 32바이트 CSPRNG → base64url. 실패하면 빈 문자열.
    std::string GenerateToken();

    /// endpoint 파일을 쓴다. 디렉터리는 만든다.
    ///
    /// ★ 토큰이 여기 들어간다. 파일 권한은 사용자 전용이어야 하고, 그 설정은
    ///   플랫폼 구현이 한다. 로그·결과·감사 어디에도 토큰을 남기지 않는다(§8).
    bool WriteEndpointFile(const std::string& path, const EndpointInfo& info, std::string& outError);

    /// 시작 시 남아 있는 파일을 확인한다.
    ///
    /// 살아 있는 pid 가 적혀 있으면 `outAliveOwner` 가 참이다 — 그때는 덮어쓰지
    /// 않는다(같은 프로젝트에 에디터가 이미 서비스를 열고 있다는 뜻이다).
    /// 죽었으면 지운다.
    bool ReclaimStaleEndpointFile(const std::string& path, bool& outAliveOwner);

    /// 정상 종료 시 지운다.
    void RemoveEndpointFile(const std::string& path) noexcept;

    /// 현재 프로세스 id.
    uint32_t CurrentProcessId() noexcept;

    /// `YYYY-MM-DDTHH:MM:SSZ`.
    std::string UtcTimestamp();
}
