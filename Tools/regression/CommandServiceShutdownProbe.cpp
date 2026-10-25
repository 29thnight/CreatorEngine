// Real Winsock/HTTP service, with a controlled gateway and no engine/GPU host.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#include "../../Engine/CommandService/CommandService.h"
#include "../../Engine/CommandService/JsonValue.h"

using namespace std::chrono_literals;

namespace
{
    int checks = 0;

    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    }

    struct Client
    {
        SOCKET socket = INVALID_SOCKET;

        explicit Client(uint16_t port)
        {
            socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Check(socket != INVALID_SOCKET, "client socket");
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            Check(connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "client connect");
            DWORD timeout = 2000;
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        }

        ~Client() { if (socket != INVALID_SOCKET) closesocket(socket); }
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        void Send(const std::string& text)
        {
            Check(CommandService::SendAll(static_cast<CommandService::SocketHandle>(socket), text.data(), text.size()), "client send");
        }

        std::string Response()
        {
            std::string response;
            while (true)
            {
                char buffer[4096];
                const int count = recv(socket, buffer, sizeof(buffer), 0);
                Check(count > 0, "HTTP response interrupted");
                response.append(buffer, count);
                const auto headerEnd = response.find("\r\n\r\n");
                const auto lengthAt = response.find("Content-Length: ");
                if (headerEnd != std::string::npos && lengthAt != std::string::npos)
                {
                    const auto length = std::stoull(response.substr(lengthAt + 16));
                    if (response.size() >= headerEnd + 4 + length) return response;
                }
            }
        }

        void ExpectClosed()
        {
            char byte{};
            const int result = recv(socket, &byte, 1, 0);
            Check(result == 0 || (result == SOCKET_ERROR && WSAGetLastError() != WSAETIMEDOUT), "server connection still open");
        }
    };

    struct Gateway final : CommandService::ICommandGateway
    {
        std::atomic<int> commandReads{0};
        bool throwOnCommands = false;
        bool largeCommands = false;

        CommandService::CommandOutcome Execute(const std::vector<std::string>&, int) override
        {
            return {200, "succeeded", "ok", "", "{}"};
        }
        bool IsLongRunning(const std::string&, bool& found) override { found = true; return true; }
        bool ExecutesUserCode(const std::string&) override { return false; }
        bool ExecuteAsync(const std::vector<std::string>&, AsyncCompletion) override { return true; }
        std::size_t QueueDepth() override { return 0; }
        std::size_t BatchQueueDepth() override { return 0; }
        void SetQueueCapacity(std::size_t) override {}
        std::string CommandJson(const std::string&) override { return "{}"; }
        HealthSnapshot Health() override { return {}; }
        std::string CommandsJson() override
        {
            commandReads.fetch_add(1);
            if (throwOnCommands) throw std::runtime_error("intentional gateway exception");
            return largeCommands ? "[\"" + std::string(8 * 1024 * 1024, 'x') + "\"]" : "[]";
        }
    };

    std::string Token(const std::filesystem::path& endpoint)
    {
        std::ifstream file(endpoint);
        const std::string text((std::istreambuf_iterator<char>(file)), {});
        const auto parsed = CommandService::ParseJson(text);
        Check(parsed.ok && parsed.value.Find("token"), "published endpoint");
        return parsed.value.Find("token")->AsString();
    }

    std::string Get(const std::string& path, const std::string& token, bool keepAlive = true)
    {
        return "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token
            + "\r\nConnection: " + (keepAlive ? "keep-alive" : "close") + "\r\n\r\n";
    }

    void RunCase(const std::filesystem::path& work, const std::string& name, CommandService::Service& service)
    {
        Gateway gateway;
        gateway.throwOnCommands = name == "gateway-exception";
        gateway.largeCommands = name == "blocked-send";
        CommandService::ServiceConfig config;
        config.projectRoot = work.string();
        config.endpointPath = (work / (name + ".endpoint.json")).string();
        config.auditLogPath = (work / "audit.log").string();
        config.idleTimeoutMs = 5000;
        config.requestDeadlineMs = 5000;
        config.streamTimeoutMs = 5000;
        std::string error;
        Check(service.Start(config, gateway, error), "service start");
        struct StopGuard
        {
            CommandService::Service& service;
            ~StopGuard() { service.Stop(); }
        } stopGuard{service};
        const auto token = Token(config.endpointPath);

        // This connection remains owned by the client until Stop returns.
        Client client(service.Port());
        if (name == "keep-alive" || name == "restart")
        {
            client.Send(Get("/health", token));
            Check(client.Response().starts_with("HTTP/1.1 200"), "authenticated keep-alive health");
        }
        else if (name == "partial-request") client.Send("GET /health HTTP/1.1\r\nHost: ");
        else if (name == "slow-request")
        {
            const auto request = Get("/health", token);
            client.Send(request.substr(0, 20));
            std::this_thread::sleep_for(350ms);
            client.Send(request.substr(20));
            Check(client.Response().starts_with("HTTP/1.1 200"), "poll slices must preserve the full request deadline");
        }
        else if (name == "closed-worker")
        {
            client.Send(Get("/health", "invalid", false));
            Check(client.Response().starts_with("HTTP/1.1 401"), "auth rejection preserved");
            client.ExpectClosed();
        }
        else if (name == "bad-request")
        {
            client.Send("GET /health HTTP/1.1\r\nmalformed-header\r\n\r\n");
            Check(client.Response().starts_with("HTTP/1.1 400"), "parser rejection preserved");
            client.ExpectClosed();
        }
        else if (name == "gateway-exception")
        {
            client.Send(Get("/commands", token));
            client.ExpectClosed();
            Check(service.IsRunning(), "worker exception must not stop service");
        }
        else if (name == "blocked-send")
        {
            client.Send(Get("/commands", token));
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (gateway.commandReads.load() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(1ms);
            Check(gateway.commandReads.load() == 1, "large response started");
        }
        else if (name == "pending-stream")
        {
            const std::string body = "{\"command\":\"pending\",\"args\":[],\"mode\":\"async\"}";
            client.Send("POST /command HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token
                + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
            const std::string response = client.Response();
            const auto accepted = CommandService::ParseJson(response.substr(response.find("\r\n\r\n") + 4));
            Check(accepted.ok && accepted.value.Find("operationId"), "pending operation accepted");
            client.Send(Get("/operations/" + accepted.value.Find("operationId")->AsString() + "/stream", token));
        }

        std::this_thread::sleep_for(100ms);
        const auto started = std::chrono::steady_clock::now();
        service.Stop();
        const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::printf("SERVICE_SHUTDOWN_CASE name=%s elapsedMs=%.3f timeoutMs=5000\n", name.c_str(), elapsedMs);
        Check(elapsedMs < 1000.0, "Stop must interrupt workers before joining; budget 1000ms");
        Check(!service.IsRunning(), "service stopped");
        Check(!std::filesystem::exists(config.endpointPath), "endpoint removed");
        service.Stop();
        Check(!service.IsRunning(), "idempotent stop");
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--exit-code")
        {
            std::this_thread::sleep_for(200ms);
            std::puts("PROCESS_EXIT_CODE_PROBE_COMPLETED");
            return std::stoi(argv[2]);
        }
        Check(argc >= 2, "work directory argument");
        const std::filesystem::path work(argv[1]);
        std::filesystem::create_directories(work);
        CommandService::SocketSubsystem sockets;
        Check(sockets.ok, "Winsock startup");
        CommandService::Service service;
        const bool canary = argc > 2 && std::string(argv[2]) == "--keepalive-only";
        if (canary) RunCase(work, "keep-alive", service);
        else
        {
            for (const char* name : {"keep-alive", "no-request", "partial-request", "slow-request", "closed-worker", "bad-request", "gateway-exception", "blocked-send", "pending-stream"})
                RunCase(work, name, service);
            for (int cycle = 0; cycle < 8; ++cycle) RunCase(work, "restart", service);
        }
        std::printf("COMMAND_SERVICE_SHUTDOWN_OK checks=%d cases=%d\n", checks, canary ? 1 : 17);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "COMMAND_SERVICE_SHUTDOWN_FAILED: %s\n", error.what());
        return 2;
    }
}
