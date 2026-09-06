#include "HttpRequest.h"

#include <algorithm>
#include <cctype>

namespace CommandService
{
    namespace
    {
        std::string ToLower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (' ' == text.front() || '\t' == text.front())) text.remove_prefix(1);
            while (!text.empty() && (' ' == text.back()  || '\t' == text.back()))  text.remove_suffix(1);
            return text;
        }

        HttpParseResult Reject(HttpParseStatus status, std::string error)
        {
            HttpParseResult result;
            result.status = status;
            result.error  = std::move(error);
            return result;
        }

        const char* ReasonPhrase(int statusCode)
        {
            switch (statusCode)
            {
            case 200: return "OK";
            case 202: return "Accepted";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 408: return "Request Timeout";
            case 409: return "Conflict";
            case 413: return "Payload Too Large";
            case 429: return "Too Many Requests";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 503: return "Service Unavailable";
            default:  return "Unknown";
            }
        }
    }

    const std::string* HttpRequest::Header(std::string_view name) const
    {
        const std::string wanted = ToLower(name);
        for (const HttpHeader& header : headers)
        {
            if (header.name == wanted) return &header.value;
        }
        return nullptr;
    }

    HttpParseResult ParseHttpRequest(std::string_view buffer, const HttpLimits& limits)
    {
        // 헤더 끝(빈 줄)을 먼저 찾는다. 없으면 더 받아야 한다.
        const std::size_t headerEnd = buffer.find("\r\n\r\n");
        if (headerEnd == std::string_view::npos)
        {
            // 헤더가 끝나지 않았는데 이미 상한을 넘었으면 더 기다리지 않는다.
            // 기다리면 헤더 폭탄 하나가 연결을 영원히 잡는다.
            if (buffer.size() > limits.maxHeaderBytes)
            {
                return Reject(HttpParseStatus::TooLarge, "헤더가 상한을 넘었다");
            }
            return HttpParseResult{};   // NeedMore
        }

        const std::string_view head = buffer.substr(0, headerEnd);
        if (head.size() > limits.maxHeaderBytes)
        {
            return Reject(HttpParseStatus::TooLarge, "헤더가 상한을 넘었다");
        }

        // ── 요청 줄 ─────────────────────────────────────────────────────
        const std::size_t lineEnd = head.find("\r\n");
        const std::string_view requestLine =
            (lineEnd == std::string_view::npos) ? head : head.substr(0, lineEnd);

        if (requestLine.size() > limits.maxRequestLine)
        {
            return Reject(HttpParseStatus::TooLarge, "요청 줄이 상한을 넘었다");
        }

        const std::size_t firstSpace = requestLine.find(' ');
        if (firstSpace == std::string_view::npos) return Reject(HttpParseStatus::BadRequest, "요청 줄 형식");
        const std::size_t secondSpace = requestLine.find(' ', firstSpace + 1);
        if (secondSpace == std::string_view::npos) return Reject(HttpParseStatus::BadRequest, "요청 줄 형식");

        HttpParseResult result;
        result.request.method = std::string(requestLine.substr(0, firstSpace));
        result.request.target = std::string(requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));

        const std::string_view version = requestLine.substr(secondSpace + 1);
        if (version.rfind("HTTP/1.", 0) != 0)
        {
            return Reject(HttpParseStatus::BadRequest, "HTTP/1.x 가 아니다");
        }
        // HTTP/1.0 은 기본이 close 다.
        result.request.keepAlive = (version != "HTTP/1.0");

        const std::size_t query = result.request.target.find('?');
        result.request.path = (query == std::string::npos)
            ? result.request.target : result.request.target.substr(0, query);

        // ── 헤더 ────────────────────────────────────────────────────────
        std::size_t at = (lineEnd == std::string_view::npos) ? head.size() : lineEnd + 2;
        while (at < head.size())
        {
            std::size_t end = head.find("\r\n", at);
            if (end == std::string_view::npos) end = head.size();

            const std::string_view line = head.substr(at, end - at);
            at = end + 2;
            if (line.empty()) continue;

            if (result.request.headers.size() >= limits.maxHeaderCount)
            {
                return Reject(HttpParseStatus::TooLarge, "헤더 개수가 상한을 넘었다");
            }

            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos)
            {
                return Reject(HttpParseStatus::BadRequest, "헤더에 ':' 이 없다");
            }

            HttpHeader header;
            header.name  = ToLower(Trim(line.substr(0, colon)));
            header.value = std::string(Trim(line.substr(colon + 1)));
            if (header.name.empty()) return Reject(HttpParseStatus::BadRequest, "헤더 이름이 비었다");
            result.request.headers.push_back(std::move(header));
        }

        // ★ chunked 를 조용히 무시하지 않는다.
        //
        //   무시하면 본문 경계를 잘못 잡는다. Content-Length 와 Transfer-Encoding
        //   이 함께 오면 어느 쪽을 믿느냐로 요청 밀수가 성립하는데, 여기서는
        //   둘 다 거부해서 그 선택 자체를 없앤다.
        if (nullptr != result.request.Header("transfer-encoding"))
        {
            return Reject(HttpParseStatus::NotImplemented, "Transfer-Encoding 은 지원하지 않는다");
        }

        // ── 본문 ────────────────────────────────────────────────────────
        std::size_t contentLength = 0;
        if (const std::string* raw = result.request.Header("content-length"))
        {
            if (raw->empty() || raw->find_first_not_of("0123456789") != std::string::npos)
            {
                return Reject(HttpParseStatus::BadRequest, "Content-Length 가 숫자가 아니다");
            }
            try
            {
                contentLength = static_cast<std::size_t>(std::stoull(*raw));
            }
            catch (const std::exception&)
            {
                return Reject(HttpParseStatus::TooLarge, "Content-Length 가 범위를 벗어났다");
            }
            if (contentLength > limits.maxBodyBytes)
            {
                return Reject(HttpParseStatus::TooLarge, "본문이 상한을 넘었다");
            }
        }

        const std::size_t bodyStart = headerEnd + 4;
        if (buffer.size() < bodyStart + contentLength)
        {
            return HttpParseResult{};   // NeedMore
        }

        result.request.body = std::string(buffer.substr(bodyStart, contentLength));
        result.consumed     = bodyStart + contentLength;

        if (const std::string* connection = result.request.Header("connection"))
        {
            const std::string lowered = ToLower(*connection);
            if (lowered.find("close") != std::string::npos)      result.request.keepAlive = false;
            else if (lowered.find("keep-alive") != std::string::npos) result.request.keepAlive = true;
        }

        result.status = HttpParseStatus::Ok;
        return result;
    }

    std::string BuildHttpResponse(int statusCode, std::string_view contentType,
                                  std::string_view body, bool keepAlive)
    {
        std::string response = "HTTP/1.1 ";
        response += std::to_string(statusCode);
        response += ' ';
        response += ReasonPhrase(statusCode);
        response += "\r\n";

        response += "Content-Type: ";
        response += contentType;
        response += "\r\n";

        response += "Content-Length: ";
        response += std::to_string(body.size());
        response += "\r\n";

        response += "Connection: ";
        response += keepAlive ? "keep-alive" : "close";
        response += "\r\n";

        // 이 응답은 브라우저가 렌더링할 것이 아니다. 캐시·스니핑·프레이밍을
        // 전부 끈다 — loopback 이라도 브라우저가 이 포트를 열 수 있다(§8).
        response += "Cache-Control: no-store\r\n";
        response += "X-Content-Type-Options: nosniff\r\n";
        response += "\r\n";

        response += body;
        return response;
    }
}
