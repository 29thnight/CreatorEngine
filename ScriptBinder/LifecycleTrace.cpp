#include "LifecycleTrace.h"
#include "Component.h"
#include "ReflectionRegister.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace Lifecycle
{
    const char* ToString(Phase phase) noexcept
    {
        switch (phase)
        {
        case Phase::Awake:       return "Awake";
        case Phase::OnEnable:    return "OnEnable";
        case Phase::Start:       return "Start";
        case Phase::FixedUpdate: return "FixedUpdate";
        case Phase::Update:      return "Update";
        case Phase::LateUpdate:  return "LateUpdate";
        case Phase::OnDisable:   return "OnDisable";
        case Phase::OnDestroy:   return "OnDestroy";
        }
        return "?";
    }

    std::atomic<bool> Trace::s_enabled{ false };

    namespace
    {
        struct Entry
        {
            uint64_t    frame;
            Phase       phase;
            const char* typeName;   // 정적 수명(컴파일 타임 문자열)
            std::string objectName; // 소유자가 먼저 죽을 수 있으므로 값으로 복사한다
            uint64_t    instanceId;
        };

        std::mutex          g_mutex;
        std::vector<Entry>  g_entries;
        uint64_t            g_frame{ 0 };
        int                 g_tickFramesRemaining{ 0 };

        // 기록이 무한정 늘지 않게 막는다. 넘어가면 그냥 멈춘다 —
        // 앞부분이 잘리면 순서 비교가 불가능해지므로 뒤를 버린다.
        constexpr size_t kMaxEntries = 2'000'000;
        bool g_overflowed{ false };

        bool IsTickPhase(Phase phase) noexcept
        {
            return phase == Phase::Update
                || phase == Phase::LateUpdate
                || phase == Phase::FixedUpdate;
        }
    }

    void Trace::Enable(int tickFrames)
    {
        {
            std::lock_guard<std::mutex> guard(g_mutex);
            g_tickFramesRemaining = (tickFrames > 0) ? tickFrames : 0;
        }
        s_enabled.store(true, std::memory_order_relaxed);
    }

    void Trace::Disable()
    {
        s_enabled.store(false, std::memory_order_relaxed);
    }

    void Trace::Clear()
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        g_entries.clear();
        g_frame = 0;
        g_overflowed = false;
    }

    void Trace::BeginFrame()
    {
        if (!IsEnabled()) return;

        std::lock_guard<std::mutex> guard(g_mutex);
        ++g_frame;
        if (g_tickFramesRemaining > 0) --g_tickFramesRemaining;
    }

    void Trace::Record(Phase phase, const char* typeName,
                       const char* objectName, uint64_t instanceId)
    {
        std::lock_guard<std::mutex> guard(g_mutex);

        // 틱 단계는 예산이 남았을 때만. 수명 단계는 항상 적는다.
        if (IsTickPhase(phase) && g_tickFramesRemaining <= 0) return;

        if (g_entries.size() >= kMaxEntries)
        {
            g_overflowed = true;
            return;
        }

        g_entries.push_back(Entry{
            g_frame,
            phase,
            (nullptr != typeName) ? typeName : "?",
            (nullptr != objectName) ? std::string(objectName) : std::string("?"),
            instanceId,
        });
    }

    size_t Trace::Count()
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        return g_entries.size();
    }

    int Trace::RemainingTickFrames() noexcept
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        return g_tickFramesRemaining;
    }

    bool Trace::Dump(const std::string& path)
    {
        std::vector<Entry> snapshot;
        bool overflowed = false;
        {
            std::lock_guard<std::mutex> guard(g_mutex);
            snapshot = g_entries;
            overflowed = g_overflowed;
        }

        std::FILE* file = nullptr;
        if (0 != ::fopen_s(&file, path.c_str(), "w") || nullptr == file) return false;

        // 형식은 diff 하기 좋게 한 줄에 한 사건, 고정 필드 순서로 둔다.
        // 인스턴스 ID는 실행마다 달라지므로 기본 비교에서는 빼고 읽는다
        // (verify-lifecycle-baseline.ps1이 그 열을 지운 뒤 비교한다).
        std::fprintf(file, "# lifecycle trace v1\n");
        std::fprintf(file, "# frame\tphase\ttype\tobject\tinstanceId\n");
        if (overflowed)
        {
            std::fprintf(file, "# WARNING: 기록 상한(%zu)에 걸려 뒤가 잘렸다\n", kMaxEntries);
        }

        for (const Entry& e : snapshot)
        {
            std::fprintf(file, "%llu\t%s\t%s\t%s\t%llu\n",
                static_cast<unsigned long long>(e.frame),
                ToString(e.phase),
                e.typeName,
                e.objectName.c_str(),
                static_cast<unsigned long long>(e.instanceId));
        }

        std::fclose(file);
        return true;
    }
}

namespace Lifecycle
{
    // 선언은 LifecycleTrace.h — 승격 근거도 그쪽 주석에 있다.
    const char* Trace::TypeNameOf(Component* component)
    {
        if (nullptr == component) return "?";
        const Meta::Type* type = Meta::Find(component->GetTypeID().m_ID_Data);
        return (nullptr != type) ? type->name.c_str() : "?";
    }
}
