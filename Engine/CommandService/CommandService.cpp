#include "CommandService.h"

#include "JsonValue.h"
#include "ServiceEndpointFile.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace CommandService
{
    namespace
    {
        constexpr const char* kJsonContentType = "application/json; charset=utf-8";

        /// 상수 시간 비교.
        ///
        /// `==` 는 첫 다른 바이트에서 멈춘다. loopback 이라도 타이밍으로 토큰을
        /// 한 바이트씩 좁히는 것은 원리적으로 가능하고, 막는 비용이 이 정도다.
        /// 길이가 다르면 즉시 거짓이되 그 판정도 분기 없이 누적한다.
        bool ConstantTimeEquals(const std::string& a, const std::string& b) noexcept
        {
            if (a.size() != b.size()) return false;
            unsigned char diff = 0;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                diff |= static_cast<unsigned char>(a[i] ^ b[i]);
            }
            return 0 == diff;
        }

        std::string ErrorBody(const std::string& code, const std::string& message)
        {
            JsonValue root = JsonValue::Object();
            root.Set("schemaVersion", JsonValue::Int(1));
            root.Set("status",  JsonValue::String("error"));
            root.Set("code",    JsonValue::String(code));
            root.Set("message", JsonValue::String(message));
            return root.Serialize();
        }
    }

    Service::~Service()
    {
        Stop();
    }

    bool Service::Start(const ServiceConfig& config, ICommandGateway& gateway, std::string& outError)
    {
        if (m_running.load(std::memory_order_acquire))
        {
            outError = "서비스가 이미 떠 있다";
            return false;
        }

        m_config  = config;
        m_gateway = &gateway;

        m_endpointPath = config.endpointPath;
        if (m_endpointPath.empty())
        {
            m_endpointPath = (std::filesystem::path(config.projectRoot)
                              / "Library" / "CommandService" / "endpoint.json").string();
        }

        // 크래시 뒤의 유령 endpoint 를 먼저 치운다. 주인이 살아 있으면 우리는 뜨지 않는다 —
        // 덮어쓰면 그쪽 클라이언트가 우리에게 오게 된다.
        bool aliveOwner = false;
        if (!ReclaimStaleEndpointFile(m_endpointPath, aliveOwner) && aliveOwner)
        {
            outError = "같은 프로젝트에 서비스가 이미 떠 있다: " + m_endpointPath;
            return false;
        }

        m_token = GenerateToken();
        if (m_token.empty())
        {
            outError = "토큰 생성 실패(CSPRNG)";
            return false;
        }

        uint16_t bound = 0;
        m_listener = ListenLoopback(config.port, config.backlog, bound, outError);
        if (!IsValid(m_listener)) return false;
        m_port = bound;

        EndpointInfo info;
        info.pid        = CurrentProcessId();
        info.port       = m_port;
        info.token      = m_token;
        info.project    = config.projectRoot;
        info.role       = config.role;
        info.startedUtc = UtcTimestamp();

        if (!WriteEndpointFile(m_endpointPath, info, outError))
        {
            CloseSocket(m_listener);
            m_listener = InvalidSocket();
            return false;
        }

        m_stopping.store(false, std::memory_order_release);
        m_running.store(true, std::memory_order_release);
        m_thread = std::thread(&Service::AcceptLoop, this);
        return true;
    }

    void Service::Stop() noexcept
    {
        if (!m_running.load(std::memory_order_acquire)) return;

        m_stopping.store(true, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();

        // 작업 스레드가 소켓과 this 를 만지므로 리스너를 닫기 전에 전부 회수한다.
        //
        // ★ 먼저 **깨운다.** 유휴 타임아웃 안에서 recv 로 막힌 스레드를 그냥
        //   기다리면 에디터 종료가 최대 30초 늦어진다. shutdown 은 recv 를
        //   즉시 반환시키고, 핸들을 닫는 것은 그 스레드가 한다.
        {
            std::lock_guard<std::mutex> guard(m_workerMutex);
            for (Worker& worker : m_workers) ShutdownSocket(worker.client);
        }
        JoinAllWorkers();

        CloseSocket(m_listener);
        m_listener = InvalidSocket();

        // 정상 종료면 파일을 지운다. 남기면 다음 클라이언트가 죽은 포트로 간다.
        RemoveEndpointFile(m_endpointPath);

        // 토큰을 메모리에서 지운다. 덤프에 남는 것을 줄인다.
        std::fill(m_token.begin(), m_token.end(), '\0');
        m_token.clear();

        m_running.store(false, std::memory_order_release);
    }

    void Service::AcceptLoop()
    {
        while (!m_stopping.load(std::memory_order_acquire))
        {
            bool        timedOut = false;
            std::string error;

            // 기한을 두는 이유: 무한 대기하는 accept 는 종료 요청을 볼 틈이 없다.
            const SocketHandle client = AcceptWithTimeout(m_listener, 200, timedOut, error);
            if (timedOut) continue;
            if (!IsValid(client)) continue;

            // bind 가 loopback 이면 원리적으로 참이지만, 그 전제가 깨졌을 때
            // 조용히 열려 있는 것보다 여기서 한 번 더 막는다.
            if (!IsLoopbackPeer(client))
            {
                m_rejected.fetch_add(1, std::memory_order_relaxed);
                Audit(0, "-", "-", 403, 0.0, "non-loopback peer");
                CloseSocket(client);
                continue;
            }

            if (m_connections.load(std::memory_order_relaxed) >= m_config.maxConnections)
            {
                const std::string body = ErrorBody("service.too_many_connections", "동시 연결 상한");
                const std::string response = BuildHttpResponse(429, kJsonContentType, body, false);
                SendAll(client, response.data(), response.size());
                CloseSocket(client);
                continue;
            }

            // ★ 연결을 **작업 스레드로 넘긴다.**
            //
            //   여기서 직접 처리하면 아무것도 안 보내는 연결 하나가 유일한
            //   스레드를 잡고, 그 동안 정상 클라이언트는 백로그에서 굶는다.
            //   인증 이전 단계라 토큰도 필요 없는 서비스 정지였다.
            ReapFinishedWorkers();

            auto finished = std::make_shared<std::atomic<bool>>(false);
            m_connections.fetch_add(1, std::memory_order_relaxed);

            Worker worker;
            worker.finished = finished;
            worker.client   = client;
            worker.thread   = std::thread([this, client, finished]
            {
                // ★ 예외가 스레드를 빠져나가면 `std::terminate` 다 — 요청 하나가
                //   에디터 전체를 죽인다. 경계를 여기 둔다.
                try
                {
                    ServeConnection(client);
                }
                catch (const std::exception& error)
                {
                    Audit(0, "-", "-", 500, 0.0, std::string("exception: ") + error.what());
                    CloseSocket(client);
                }
                catch (...)
                {
                    Audit(0, "-", "-", 500, 0.0, "unknown exception");
                    CloseSocket(client);
                }
                m_connections.fetch_sub(1, std::memory_order_relaxed);
                finished->store(true, std::memory_order_release);
            });

            {
                std::lock_guard<std::mutex> guard(m_workerMutex);
                m_workers.push_back(std::move(worker));
            }
        }

        JoinAllWorkers();
    }

    void Service::ReapFinishedWorkers()
    {
        std::lock_guard<std::mutex> guard(m_workerMutex);
        for (auto it = m_workers.begin(); it != m_workers.end();)
        {
            if (it->finished->load(std::memory_order_acquire))
            {
                if (it->thread.joinable()) it->thread.join();
                it = m_workers.erase(it);
            }
            else ++it;
        }
    }

    void Service::JoinAllWorkers()
    {
        std::vector<Worker> workers;
        {
            std::lock_guard<std::mutex> guard(m_workerMutex);
            workers.swap(m_workers);
        }
        for (Worker& worker : workers)
        {
            if (worker.thread.joinable()) worker.thread.join();
        }
    }

    void Service::ServeConnection(SocketHandle client)
    {
        const uint16_t peerPort = PeerPort(client);

        std::string buffer;
        bool        keepAlive = true;

        while (keepAlive && !m_stopping.load(std::memory_order_acquire))
        {
            HttpParseResult parsed = ParseHttpRequest(buffer, m_config.limits);

            // ★ 요청 하나의 **절대 기한**. 유휴 타임아웃과 다른 물건이다.
            //
            //   `recv` 마다 30초를 새로 주면 29초마다 1바이트씩 흘리는 연결이
            //   영원히 산다. 그 연결은 인증 이전이라 토큰도 필요 없다.
            //   머리부터 본문 끝까지가 이 기한 안에 도착해야 한다.
            const auto requestDeadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(m_config.requestDeadlineMs);
            bool firstByteSeen = !buffer.empty();

            while (HttpParseStatus::NeedMore == parsed.status)
            {
                if (firstByteSeen && std::chrono::steady_clock::now() > requestDeadline)
                {
                    const std::string body = ErrorBody("request.deadline",
                                                       "요청이 기한 안에 도착하지 않았다");
                    const std::string response = BuildHttpResponse(408, kJsonContentType, body, false);
                    SendAll(client, response.data(), response.size());
                    Audit(peerPort, "-", "-", 408, 0.0, "request deadline");
                    CloseSocket(client);
                    return;
                }

                // 유휴(첫 바이트 전)는 keep-alive 대기이므로 idleTimeout 을 쓰고,
                // 요청이 시작된 뒤에는 남은 기한만큼만 기다린다.
                int waitMs = m_config.idleTimeoutMs;
                if (firstByteSeen)
                {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        requestDeadline - std::chrono::steady_clock::now()).count();
                    waitMs = static_cast<int>((remaining > 0) ? remaining : 1);
                }

                char chunk[4096];
                const int got = Receive(client, chunk, sizeof(chunk), waitMs);
                if (got <= 0)
                {
                    // 0 = 상대가 닫음, -1 = 오류, -2 = 유휴 타임아웃. 셋 다 종료다.
                    CloseSocket(client);
                    return;
                }
                firstByteSeen = true;
                buffer.append(chunk, static_cast<std::size_t>(got));

                if (buffer.size() > m_config.limits.maxHeaderBytes + m_config.limits.maxBodyBytes)
                {
                    const std::string body = ErrorBody("request.too_large", "요청이 상한을 넘었다");
                    const std::string response = BuildHttpResponse(413, kJsonContentType, body, false);
                    SendAll(client, response.data(), response.size());
                    Audit(peerPort, "-", "-", 413, 0.0, "oversize");
                    CloseSocket(client);
                    return;
                }
                parsed = ParseHttpRequest(buffer, m_config.limits);
            }

            if (HttpParseStatus::Ok != parsed.status)
            {
                const int status =
                    (HttpParseStatus::TooLarge == parsed.status)       ? 413 :
                    (HttpParseStatus::NotImplemented == parsed.status) ? 501 : 400;
                const std::string body = ErrorBody("request.invalid", parsed.error);
                const std::string response = BuildHttpResponse(status, kJsonContentType, body, false);
                SendAll(client, response.data(), response.size());
                Audit(peerPort, "-", "-", status, 0.0, parsed.error);
                CloseSocket(client);
                return;
            }

            const auto started = std::chrono::steady_clock::now();
            const std::string response = HandleRequest(parsed.request, peerPort);
            const std::chrono::duration<double, std::milli> elapsed =
                std::chrono::steady_clock::now() - started;
            (void)elapsed;

            if (!SendAll(client, response.data(), response.size()))
            {
                CloseSocket(client);
                return;
            }

            keepAlive = parsed.request.keepAlive;
            buffer.erase(0, parsed.consumed);
        }

        CloseSocket(client);
    }

    bool Service::Authorized(const HttpRequest& request) const
    {
        const std::string* header = request.Header("authorization");
        if (nullptr == header) return false;

        constexpr std::string_view kPrefix = "Bearer ";
        if (header->size() <= kPrefix.size()) return false;
        if (0 != header->compare(0, kPrefix.size(), kPrefix)) return false;

        return ConstantTimeEquals(header->substr(kPrefix.size()), m_token);
    }

    std::string Service::HandleRequest(const HttpRequest& request, uint16_t peerPort)
    {
        const auto started = std::chrono::steady_clock::now();

        const auto finish = [&](int status, const std::string& body, const std::string& note)
        {
            const std::chrono::duration<double, std::milli> elapsed =
                std::chrono::steady_clock::now() - started;
            Audit(peerPort, request.method, request.path, status, elapsed.count(), note);
            return BuildHttpResponse(status, kJsonContentType, body, request.keepAlive);
        };

        // ★ 브라우저를 먼저 막는다.
        //
        //   토큰이 없으면 대부분 401 로 끝나지만, 토큰 없이 답하는 경로가 하나라도
        //   생기면 웹페이지가 로컬 에디터를 조작할 수 있다. `Origin`/`Referer` 가
        //   붙은 요청은 사람이 `curl` 로 부른 것이 아니다.
        if (nullptr != request.Header("origin") || nullptr != request.Header("referer"))
        {
            m_rejected.fetch_add(1, std::memory_order_relaxed);
            return finish(403, ErrorBody("request.browser_origin",
                                         "Origin/Referer 가 있는 요청은 받지 않는다"), "origin");
        }

        // ★ `/health` 도 예외가 아니다.
        if (!Authorized(request))
        {
            m_rejected.fetch_add(1, std::memory_order_relaxed);
            return finish(401, ErrorBody("auth.invalid_token", "토큰이 없거나 맞지 않는다"), "unauthorized");
        }

        if (nullptr == m_gateway)
        {
            return finish(503, ErrorBody("service.no_gateway", "게이트웨이가 없다"), "no-gateway");
        }

        // ── GET /health ─────────────────────────────────────────────────
        if ("GET" == request.method && "/health" == request.path)
        {
            const ICommandGateway::HealthSnapshot health = m_gateway->Health();

            JsonValue root = JsonValue::Object();
            root.Set("schemaVersion",  JsonValue::Int(1));
            root.Set("role",           JsonValue::String(health.role));
            root.Set("pid",            JsonValue::Int(static_cast<int64_t>(CurrentProcessId())));
            root.Set("frame",          JsonValue::Int(static_cast<int64_t>(health.frame)));
            root.Set("state",          JsonValue::String(health.state));
            root.Set("blockedReason",  JsonValue::String(health.blockedReason));
            root.Set("queueDepth",     JsonValue::Int(static_cast<int64_t>(health.queueDepth)));
            root.Set("oldestQueuedMs", JsonValue::Double(health.oldestQueuedMs));
            root.Set("currentCommand", JsonValue::String(health.currentCommand));
            return finish(200, root.Serialize(), "");
        }

        // ── GET /commands, GET /commands/{id} ───────────────────────────
        if ("GET" == request.method && 0 == request.path.rfind("/commands", 0))
        {
            if ("/commands" == request.path)
            {
                return finish(200, m_gateway->CommandsJson(), "");
            }
            if (0 == request.path.rfind("/commands/", 0))
            {
                const std::string name = request.path.substr(std::string("/commands/").size());
                const std::string json = m_gateway->CommandJson(name);
                if (json.empty())
                {
                    return finish(404, ErrorBody("command.unknown", "알 수 없는 명령: " + name), "unknown");
                }
                return finish(200, json, "");
            }
        }

        // ── POST /command ───────────────────────────────────────────────
        if ("POST" == request.method && "/command" == request.path)
        {
            const std::string* contentType = request.Header("content-type");
            if (nullptr == contentType || contentType->find("application/json") == std::string::npos)
            {
                return finish(400, ErrorBody("request.content_type",
                                             "Content-Type 은 application/json 이어야 한다"), "content-type");
            }

            const JsonParseResult parsed = ParseJson(request.body);
            if (!parsed.ok)
            {
                return finish(400, ErrorBody("request.bad_json", parsed.error), "bad-json");
            }

            const JsonValue* command = parsed.value.Find("command");
            if (nullptr == command || JsonValue::Kind::String != command->GetKind()
                || command->AsString().empty())
            {
                return finish(400, ErrorBody("request.command_missing",
                                             "command 는 비지 않은 문자열이어야 한다"), "no-command");
            }

            // ★ args 는 문자열 배열이다. 라인 문자열을 받지 않는다(§5.2).
            //   여기서 이어 붙였다 다시 자르면 §3.2 의 왕복 손실이 되살아난다.
            std::vector<std::string> arguments;
            arguments.push_back(command->AsString());

            if (const JsonValue* args = parsed.value.Find("args"))
            {
                if (JsonValue::Kind::Array != args->GetKind())
                {
                    return finish(400, ErrorBody("request.args_type", "args 는 배열이어야 한다"), "args-type");
                }
                for (const JsonValue& item : args->Items())
                {
                    if (JsonValue::Kind::String != item.GetKind())
                    {
                        return finish(400, ErrorBody("request.args_type",
                                                     "args 의 원소는 문자열이어야 한다"), "args-type");
                    }
                    arguments.push_back(item.AsString());
                }
            }

            int timeoutMs = m_config.defaultCommandTimeoutMs;
            if (const JsonValue* value = parsed.value.Find("timeoutMs"))
            {
                if (JsonValue::Kind::Int != value->GetKind() || value->AsInt() <= 0)
                {
                    return finish(400, ErrorBody("request.timeout_type",
                                                 "timeoutMs 는 양의 정수여야 한다"), "timeout-type");
                }
                timeoutMs = static_cast<int>(value->AsInt());
            }

            std::string correlationId;
            if (const JsonValue* value = parsed.value.Find("correlationId"))
            {
                if (JsonValue::Kind::String == value->GetKind()) correlationId = value->AsString();
            }

            const CommandOutcome outcome = m_gateway->Execute(arguments, timeoutMs);

            JsonValue root = JsonValue::Object();
            root.Set("schemaVersion", JsonValue::Int(1));
            if (!correlationId.empty()) root.Set("correlationId", JsonValue::String(correlationId));
            root.Set("command", JsonValue::String(arguments.front()));
            root.Set("status",  JsonValue::String(outcome.status));
            root.Set("code",    JsonValue::String(outcome.code));
            root.Set("message", JsonValue::String(outcome.message));

            // data 는 어댑터가 이미 직렬화했다. 다시 파싱하지 않고 넣는다 —
            // 여기서 파싱하면 왕복이 하나 더 생기고, 그 왕복마다 손실이 가능하다.
            const JsonParseResult data = ParseJson(outcome.dataJson.empty() ? "{}" : outcome.dataJson);
            root.Set("data", data.ok ? data.value : JsonValue::Object());

            // timing 은 장식이 아니라 지연 계약의 증거다(§5.2).
            JsonValue timing = JsonValue::Object();
            timing.Set("queuedMs",     JsonValue::Double(outcome.queuedMs));
            timing.Set("waitedFrames", JsonValue::Int(static_cast<int64_t>(outcome.waitedFrames)));
            timing.Set("executedMs",   JsonValue::Double(outcome.executedMs));
            root.Set("timing", std::move(timing));

            return finish(outcome.httpStatus, root.Serialize(),
                          outcome.timedOut ? "timeout" : "");
        }

        return finish(404, ErrorBody("route.unknown",
                                     "없는 경로: " + request.method + " " + request.path), "no-route");
    }

    void Service::Audit(uint16_t peerPort, const std::string& method, const std::string& path,
                        int statusCode, double elapsedMs, const std::string& note)
    {
        std::string logPath = m_config.auditLogPath;
        if (logPath.empty())
        {
            logPath = (std::filesystem::path(m_endpointPath).parent_path() / "audit.log").string();
        }

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(logPath).parent_path(), ec);

        std::ofstream file(logPath, std::ios::app | std::ios::binary);
        if (!file) return;

        // ★ 토큰과 인자 원문은 남기지 않는다(§8).
        //   경로와 상태·소요만으로 "누가 무엇을 언제"는 재구성되고, 인자에는
        //   자산 경로·오브젝트 이름처럼 남길 이유가 없는 것이 섞인다.
        // 경로는 공격자가 정한다. 제어 문자가 들어오면 탭 구분 표가 어긋난다.
        std::string safePath;
        safePath.reserve(path.size());
        for (const char c : path)
        {
            safePath.push_back((static_cast<unsigned char>(c) < 0x20) ? '?' : c);
        }

        char line[512] = {};
        std::snprintf(line, sizeof(line), "%s\tport=%u\t%s\t%s\t%d\t%.3fms\t%s\n",
                      UtcTimestamp().c_str(), static_cast<unsigned>(peerPort),
                      method.c_str(), safePath.c_str(), statusCode, elapsedMs, note.c_str());
        file.write(line, static_cast<std::streamsize>(std::char_traits<char>::length(line)));
    }
}
