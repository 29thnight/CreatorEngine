#pragma once
// LC4 (PHASE 14.5) — 로컬 HTTP/JSON 명령 서비스.
//
// ── 이것은 실행 표면이다 (§8) ───────────────────────────────────────────
//
// HTTP 로 열리는 순간 `MutatesAssets` 명령이 프로세스 경계 밖에서 호출 가능해진다.
// 그래서 아래는 기능이 아니라 **전제**다.
//
//   · bind 는 loopback 뿐. 주소는 설정이 아니라 상수다(SocketPlatform).
//   · 요청마다 `Authorization: Bearer <token>`. 상수 시간 비교.
//     **`/health` 도 예외가 아니다** — 토큰 없이 답하는 경로가 하나라도 생기면
//     웹페이지가 로컬 에디터의 상태를 긁을 수 있다.
//   · `Origin`/`Referer` 가 있으면 거부. 브라우저가 localhost 로 요청을 보내는
//     것 자체는 막히지 않는다.
//   · 본문 1MiB · 헤더 8KiB · 헤더 64개 · 유휴 30초 · 동시 연결 상한.
//   · 서비스는 **기본 off**. 켜는 것은 명시 플래그다.
//   · 감사 로그에 토큰과 인자 원문을 남기지 않는다.

#include "CommandGateway.h"
#include "OperationTable.h"
#include "HttpRequest.h"
#include "SocketPlatform.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace CommandService
{
    struct ServiceConfig
    {
        std::string  role{ "editor" };
        std::string  projectRoot;
        std::string  endpointPath;      ///< 비면 projectRoot/Library/CommandService/endpoint.json
        std::string  auditLogPath;      ///< 비면 endpoint 옆 audit.log

        uint16_t     port{ 0 };         ///< 0 이면 OS 가 고른다
        int          backlog{ 8 };
        int          maxConnections{ 16 };
        int          idleTimeoutMs{ 30000 };
        int          defaultCommandTimeoutMs{ 5000 };

        /// 요청 하나가 **머리부터 본문 끝까지** 도착하는 데 허용되는 총 시간.
        ///
        /// ★ 유휴 타임아웃만으로는 못 막는다. `recv` 마다 30초를 새로 주면,
        ///   29초마다 1바이트씩 흘리는 연결이 **영원히** 산다. 인증 이전 단계라
        ///   토큰도 필요 없다. 절대 기한을 따로 둔다.
        int          requestDeadlineMs{ 10000 };

        int          maxQueueDepth{ 64 };     ///< 429 (§7.3)
        int          streamTimeoutMs{ 60000 };

        /// `ExecutesUserCode` 명령을 받을 것인가. **기본은 거부(403)** 다(§8).
        ///
        /// ★ 서비스를 켠 것과 사용자 코드를 열어 준 것은 다른 결정이다.
        ///
        ///   `--command-service` 는 "이 프로세스를 자동화하겠다" 이고, 그 안에는
        ///   씬을 열고 자산을 저작하는 것까지 들어 있다. 거기에 "게임 스크립트
        ///   어셈블리의 표식된 메서드를 부른다" 를 얹으면 표면의 성질이 바뀐다 —
        ///   앞의 것들은 엔진이 쓴 코드가 하는 일이고, 뒤의 것은 엔진이 쓰지
        ///   않은 코드가 하는 일이다. 두 결정을 한 플래그에 묶으면 서비스를 켜는
        ///   모든 실행이 뒤엣것에 동의한 셈이 된다.
        bool         allowUserCode{ false };

        HttpLimits   limits;
    };

    class Service
    {
    public:
        Service() = default;
        ~Service();

        Service(const Service&)            = delete;
        Service& operator=(const Service&) = delete;

        /// 수신 스레드를 띄운다. 실패하면 false 와 사유.
        ///
        /// 게이트웨이는 호출자가 소유하고 서비스보다 오래 살아야 한다.
        bool Start(const ServiceConfig& config, ICommandGateway& gateway, std::string& outError);

        void Stop() noexcept;

        bool     IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }
        uint16_t Port()      const noexcept { return m_port; }

        /// 인증 실패 누적. 감사와 게이트가 읽는다.
        uint64_t RejectedCount() const noexcept { return m_rejected.load(std::memory_order_relaxed); }

    private:
        void AcceptLoop();
        void ServeConnection(SocketHandle client);

        /// 요청 하나를 처리해 응답 문자열을 만든다.
        std::string HandleRequest(const HttpRequest& request, uint16_t peerPort);

        /// 토큰 검사. 상수 시간 비교 — 길이만으로도 정보가 새지 않게 한다.
        bool Authorized(const HttpRequest& request) const;

        void Audit(uint16_t peerPort, const std::string& method, const std::string& path,
                   int statusCode, double elapsedMs, const std::string& note);

        ServiceConfig     m_config;
        ICommandGateway*  m_gateway{ nullptr };

        SocketHandle      m_listener{ 0 };
        uint16_t          m_port{ 0 };
        std::string       m_token;
        std::string       m_endpointPath;

        std::thread       m_thread;
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_stopping{ false };
        std::atomic<int>  m_connections{ 0 };
        std::atomic<uint64_t> m_rejected{ 0 };

        /// 진행 중 operation 표.
        ///
        /// ★ **값이 아니라 shared_ptr 인 이유가 수명이다.**
        ///
        ///   `ExecuteAsync` 가 넘기는 completion 은 게임 스레드가 그 명령을
        ///   드레인할 때 불린다. 그 시점은 큐 적체·씬 로딩에 따라 얼마든지
        ///   뒤로 밀린다. 표를 값 멤버로 두고 람다가 `&m_operations` 를 담으면,
        ///   그 사이에 `Service` 가 사라지는 순간 use-after-free 다. 오늘은
        ///   호출자가 magic static 하나뿐이라 안 터지지만, 그것은 **이 클래스가
        ///   보장하는 성질이 아니라 호출자의 우연**이다. LC8 의 Player 가
        ///   레벨 전환마다 서비스를 껐다 켜면 바로 드러난다.
        std::shared_ptr<OperationTable> m_operations{ std::make_shared<OperationTable>() };

        /// 연결 하나를 맡은 작업 스레드.
        ///
        /// ★ 처음에는 수신 루프가 연결을 **직접** 처리했다. 그러면 아무 로컬
        ///   프로세스나 연결만 열고 아무것도 안 보내도 그 하나가 유일한 스레드를
        ///   잡는다 — 인증 이전이라 토큰도 필요 없는 서비스 정지다. 게다가
        ///   `maxConnections` 검사는 동시 연결이 1 을 넘을 수 없어 죽은 코드였다.
        struct Worker
        {
            std::thread                        thread;
            std::shared_ptr<std::atomic<bool>> finished;
            SocketHandle                       client{};
        };
        std::mutex          m_workerMutex;
        std::vector<Worker> m_workers;

        void ReapFinishedWorkers();
        void JoinAllWorkers();
    };
}
