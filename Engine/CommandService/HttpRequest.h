#pragma once
// LC4 (PHASE 14.5) — HTTP/1.1 최소 서브셋 파서.
//
// ── 표면을 좁게 고정한다 ────────────────────────────────────────────────
//
// §17 이 HTTP 라이브러리를 기각하며 감수한 위험이 "자체 파서의 결함"이다.
// 그 위험을 통제하는 방법은 **표면을 작게 유지하는 것**이고, 그래서 여기서
// 지원하는 것은 다음뿐이다.
//
//   · `GET` 과 `POST`
//   · `Content-Length` 본문 (chunked 없음 — 필요가 없고, chunked 파서는
//     이 표면에서 가장 흔한 결함 자리다)
//   · `Connection: keep-alive` / `close`
//   · 헤더 개수·길이·본문 크기 상한
//
// 지원하지 않는 것을 조용히 무시하지 않는다. `Transfer-Encoding` 이 오면
// 거부한다 — 무시하면 본문 경계를 잘못 잡고, 그것이 요청 밀수(smuggling)의
// 고전적 형태다. loopback 전용이라도 값싸게 막을 수 있으면 막는다.
//
// ── 한계는 기본값이 아니라 타입의 일부다 ────────────────────────────────
//
// 상한을 파서 바깥의 호출자가 기억해야 하면 언젠가 잊는다. `HttpLimits` 를
// 인자로 받게 해서 잊을 수 없게 한다.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CommandService
{
    struct HttpLimits
    {
        std::size_t maxRequestLine{ 8 * 1024 };
        std::size_t maxHeaderBytes{ 8 * 1024 };
        std::size_t maxHeaderCount{ 64 };
        std::size_t maxBodyBytes{ 1024 * 1024 };   ///< 1 MiB (§8)
    };

    struct HttpHeader
    {
        std::string name;    ///< 소문자로 정규화
        std::string value;
    };

    struct HttpRequest
    {
        std::string             method;
        std::string             target;    ///< 경로 + 쿼리
        std::string             path;      ///< 쿼리를 뗀 경로
        std::vector<HttpHeader> headers;
        std::string             body;
        bool                    keepAlive{ true };

        const std::string* Header(std::string_view name) const;
    };

    enum class HttpParseStatus : uint8_t
    {
        Ok,
        NeedMore,        ///< 아직 헤더/본문이 다 안 왔다
        BadRequest,      ///< 400
        TooLarge,        ///< 413
        NotImplemented,  ///< 501 — chunked 등 지원하지 않는 것
    };

    struct HttpParseResult
    {
        HttpParseStatus status{ HttpParseStatus::NeedMore };
        HttpRequest     request;
        std::size_t     consumed{ 0 };   ///< 버퍼에서 소비한 바이트(keep-alive 용)
        std::string     error;
    };

    /// 버퍼에서 요청 하나를 읽는다. 부분 입력은 `NeedMore` 다.
    HttpParseResult ParseHttpRequest(std::string_view buffer, const HttpLimits& limits);

    /// 응답 한 덩이를 만든다. `Connection` 은 호출자가 정한다.
    std::string BuildHttpResponse(int statusCode,
                                  std::string_view contentType,
                                  std::string_view body,
                                  bool keepAlive);
}
