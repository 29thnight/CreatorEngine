#pragma once
#ifndef BUILD_FLAG

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <cstdint>

class GpuProfilerD3D11 {
private:
    using ComPtr = Microsoft::WRL::ComPtr<ID3D11Query>;
public:
    struct ResolvedEvent {
        std::string name;
        double beginMs = 0.0;
        double endMs = 0.0;
        uint16_t depth = 0;
    };

    struct Pending {
        std::string name;
        ComPtr qBegin;
        ComPtr qEnd;
        uint16_t depth = 0;
    };

    struct Frame {
        ComPtr qDisjoint;
        ComPtr qFrameStart;    // TIMESTAMP (프레임 기준 0점)
        UINT64 frameStartTicks = 0;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        bool disjointEnded = false;

        std::vector<ComPtr> tsPool;   // TIMESTAMP 쿼리 풀 (2 * maxEvents)
        uint32_t nextTs = 0;

        std::vector<Pending>      pending;   // 아직 GetData 안 한 이벤트
        std::vector<ResolvedEvent> resolved;  // HUD에서 그릴 결과
    };


    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* immediate,
        uint32_t historySize = 240, uint32_t maxEventsPerFrame = 4096);
    void Shutdown();

    // 프레임 경계(렌더링 시작 직후/Present 직전)
    void OnFrameStart(ID3D11DeviceContext* ctx);
    void OnFrameEnd(ID3D11DeviceContext* ctx);

    // N 프레임 뒤의 결과를 비차단으로 해석(프레임 시작에 호출 권장)
    void ResolvePending(uint32_t framesLatency = 2);

    // 구간 RAII
    uint32_t BeginEvent(ID3D11DeviceContext* ctx, const char* name);
    void     EndEvent(ID3D11DeviceContext* ctx, uint32_t token);

    // 디퍼드 컨텍스트 스트림 (스레드 로컬)
    void     StartDeferredStream();
    struct GpuStreamHandle { std::vector<Pending> events; uint32_t frameIndex = 0; };
    GpuStreamHandle EndDeferredStream();
    void     AttachStream(GpuStreamHandle&& h);

    // HUD 접근
    const std::vector<ResolvedEvent>& GetResolved(uint32_t historyIndex) const;

    // 현재 히스토리 인덱스 (0..History-1, 최신 프레임)
    uint32_t CurHistoryIndex() const { return m_frameIndex; }
    uint32_t HistorySize() const { return m_historySize; }

    ComPtr NewTimestampQuery();
    ComPtr NewDisjointQuery();

    Frame& CurFrame() { return m_frames[m_frameIndex]; }
    Frame& OldFrame(uint32_t age) { return m_frames[(m_frameIndex + m_historySize - (age % m_historySize)) % m_historySize]; }
    ComPtr AcquireTs(Frame& f);
    ComPtr AcquireTsTLS();

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_immediate = nullptr;
    std::vector<Frame>    m_frames;
    uint32_t              m_frameIndex = 0;
    uint32_t              m_historySize = 0;
    uint32_t              m_maxEventsPerFrame = 0;

    struct Tls {
        std::vector<uint32_t> stack;
        std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> tsPool;
        uint32_t nextTs = 0;
        std::vector<Pending> local; // local events recorded on this thread
    };
    static thread_local Tls s_tls;
};

// 전역 인스턴스
extern GpuProfilerD3D11 gGpuProfiler;

// 매크로
struct GPUProfileScope {
    GpuProfilerD3D11* p = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    uint32_t token = ~0u;
    GPUProfileScope(GpuProfilerD3D11* profiler, ID3D11DeviceContext* c, const char* name)
        : p(profiler), ctx(c) {
        token = p->BeginEvent(ctx, name);
    }
    ~GPUProfileScope() { if (p) p->EndEvent(ctx, token); }
};

#define PROFILE_GPU_FRAME_START(ctx) gGpuProfiler.OnFrameStart((ctx))
#define PROFILE_GPU_FRAME_END(ctx)   gGpuProfiler.OnFrameEnd((ctx))
#define PROFILE_GPU_SCOPE(ctx, name) GPUProfileScope _gpuScope_((&gGpuProfiler),(ctx),(name))

#else

// BUILD_FLAG defined: strip everything
class GpuProfilerD3D11 { public: struct ResolvedEvent {}; };
#define PROFILE_GPU_FRAME_START(ctx)  do{}while(0)
#define PROFILE_GPU_FRAME_END(ctx)    do{}while(0)
#define PROFILE_GPU_SCOPE(ctx, name)  do{}while(0)

#endif // BUILD_FLAG
