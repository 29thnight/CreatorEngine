#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../RHIPipelineState.h"
#include "RenderFrameServices.h"
#include <cstdint>
#include <string>
#include <vector>
#include <future>
#include <mutex>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>

// 전방 선언. 유니티 빌드에서는 옆 파일이 넣어 주어 가려져 있었다(비유니티 점검).
class DX12DeviceResources;

// PSO 중앙 관리 + 캐시(PHASE 3-4).
//
// DX11에는 파이프라인 상태 객체라는 개념이 없어서 기존 ShaderSystem의
// ShaderPSO/VisualShaderPSO는 이름만 PSO인 '셰이더 묶음'이다. 진짜 PSO는 DX12에서
// 처음 생기며, 그 생성 비용(수 ms~수십 ms)이 프레임 중에 터지면 스톨이 된다.
// 그래서 두 가지를 한다: 상태 조합을 해시로 중복 제거하고, 컴파일 결과를 디스크에
// 남겨 두 번째 실행부터는 컴파일 자체를 없앤다.
//
// 캐시 2층:
//   메모리  해시 → PSO 포인터. 같은 실행 안에서의 중복 요청을 막는다.
//   디스크  ID3D12PipelineLibrary. 드라이버·어댑터가 바뀌면 로드가 실패하고
//           그때는 빈 라이브러리로 시작한다(무효화는 런타임이 판정해 준다).

// ★ `RHIGraphicsPipelineDesc` · `RHIComputePipelineDesc` 가 여기 있었다.
//   A-1 에서 `RHI/RHIPipelineState.h` 의 `RHIGraphicsPipelineDesc` ·
//   `RHIComputePipelineDesc` 로 올라갔다 — V6 이 필드의 어휘를 이미 중립으로
//   갈아 두었으므로, 남은 일은 객체 필드 하나를 핸들로 바꾸는 것뿐이었다.
//
// ★ `ComputeHash()` 가 desc 의 멤버가 아니게 됐다. 레이아웃 핸들을 **안정
//   해시로 풀어야** 하는데 그것은 표를 봐야 알 수 있고, 표는 백엔드의 것이다.
//   즉 해시는 desc 혼자 답할 수 있는 질문이 아니게 됐다 — 아래 매니저의
//   private 메서드로 내려갔다.

class DX12PSOManager : public IRenderPipelineCache
{
public:
    struct Stats
    {
        uint32_t memoryHits{ 0 };   // 같은 실행 안에서 재사용
        uint32_t libraryHits{ 0 };  // 디스크 캐시에서 복원 — 두 번째 실행의 목표치
        uint32_t compiles{ 0 };     // 실제 드라이버 컴파일 — 재실행에서 0이어야 한다
        uint32_t failures{ 0 };
        uint32_t fallbackDraws{ 0 };  // 폴백으로 그린 드로우 — 지속적으로 늘면 캐시가 놀고 있다
        uint32_t skippedDraws{ 0 };   // 폴백조차 없어 건너뛴 드로우

        // 스키마 도장이 달라 통째로 버린 캐시 파일. 매 실행 1이면 도장이
        // 실행마다 달라지고 있다는 뜻이고, 그러면 디스크 캐시가 늘 논다.
        uint32_t cacheDiscarded{ 0 };
    };

    // 비동기 요청의 상태. Pending 프레임은 폴백 PSO를 쓰거나 그 드로우를 건너뛴다 —
    // 어느 쪽이든 컴파일 스톨이 프레임에 들어오지 않는 것이 목적이다.
    enum class RequestState { Ready, Pending, Failed };

    /// ★ 디바이스가 아니라 DX12DeviceResources 를 받는다(A-1). 발급한 핸들을
    ///   등록할 표가 거기 있기 때문이고, DX12MeshCache · DX12TextureCache 가
    ///   이미 같은 모양이다.
    bool Initialize(DX12DeviceResources* resources, const std::wstring& cacheFilePath,
        std::string& outError);
    void Shutdown();

    // 동기 취득 — 미스면 이 자리에서 컴파일한다(로딩 시점용).
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc& desc, std::string& outError) override;

    // 컴퓨트 PSO. 같은 캐시 2층을 공유한다.
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc& desc, std::string& outError) override;

    // 비동기 요청 — 준비됐으면 Ready + 포인터, 아니면 Pending(컴파일은 백그라운드로).
    //
    // ★ 여기는 원시 포인터를 그대로 낸다. 인터페이스가 아니라 이 매니저 자신의
    //   표면이고, 호출자가 `dx12.psocache` 하나뿐이다(실측). 핸들로 바꾸면
    //   소비자 없는 자리를 하나 더 만드는 것이라 A-1 의 범위 밖이다.
    RequestState Request(const RHIGraphicsPipelineDesc& desc, ID3D12PipelineState** outPso);

    // ── 폴백 정책 ──
    //
    // 비동기 컴파일이 끝나지 않은 프레임에 무엇을 그릴지의 답이다. 폴백을 등록해
    // 두면 Pending 대신 폴백 PSO가 나와 그 드로우를 계속 그릴 수 있고(보통 단순
    // 셰이더), 등록하지 않았으면 호출부가 그 드로우를 건너뛴다.
    //
    // 어느 쪽이든 프레임이 컴파일을 기다리지 않는 것이 핵심이다.
    bool SetFallback(const RHIGraphicsPipelineDesc& desc, std::string& outError);

    enum class DrawDecision { UseRequested, UseFallback, Skip };

    // 프레임 기록 경로에서 쓰는 형태. 요청이 준비됐으면 그것을, 아니면 폴백을,
    // 폴백도 없으면 Skip을 돌려준다.
    DrawDecision Resolve(const RHIGraphicsPipelineDesc& desc, ID3D12PipelineState** outPso);

    // 셰이더 리로드 시 메모리 캐시를 비운다.
    //
    // 리로드된 셰이더는 바이트코드가 달라 해시부터 다르므로 '잘못된 PSO를 쓸' 위험은
    // 없다. 문제는 옛 PSO가 캐시에 남아 메모리만 먹는 것이라, 리로드를 계기로 비운다.
    // 디스크 라이브러리는 건드리지 않는다 — 키가 다르니 공존해도 무해하고, 되돌릴 때
    // 다시 히트한다.
    void OnShaderReloaded();

    // 라이브러리를 파일로 남긴다. 종료 시 1회.
    bool SaveCache(std::string& outError);

    Stats GetStats() const;
    bool  IsLibraryLoaded() const { return m_libraryLoadedFromDisk; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // 라이브러리 이름은 해시에서 유도한다 — 실행 간 안정적이어야 복원이 된다.
    static std::wstring MakeLibraryName(uint64_t hash);

    // 라이브러리 로드 → 실패 시 컴파일 → 라이브러리 저장. 락 밖에서 부른다.
    ComPtr<ID3D12PipelineState> CreateOne(const RHIGraphicsPipelineDesc& desc,
        uint64_t hash, std::string& outError);
    ComPtr<ID3D12PipelineState> CreateOneCompute(const RHIComputePipelineDesc& desc,
        uint64_t hash, std::string& outError);

    /// ★ desc 의 멤버가 아니라 여기 있는 이유(A-1): 레이아웃 핸들을 **안정
    ///   해시**로 풀어야 하는데 그 해시는 표가 든다. 핸들 자체를 해시하면
    ///   슬롯 번호가 실행마다 달라져 디스크 캐시가 매 실행 논다 —
    ///   `dx12.psocache` 의 "2회차 컴파일 0건"이 그것을 잡는다.
    uint64_t ComputeHash(const RHIGraphicsPipelineDesc& desc) const;
    uint64_t ComputeHash(const RHIComputePipelineDesc& desc) const;

    /// 핸들 → 루트 시그니처. 표가 없으면 nullptr 이고, 그러면 생성이 실패한다.
    ID3D12RootSignature* ResolveSignature(RHIPipelineLayoutHandle layout) const;
    uint64_t ResolveStableHash(RHIPipelineLayoutHandle layout) const;

    /// 캐시에 넣고 핸들을 발급한다. **락을 쥔 채로** 부른다.
    RHIPipelineHandle Publish(uint64_t hash, ComPtr<ID3D12PipelineState> pso,
        RHIPipelineLayoutHandle layout, std::string& outError);

    class DX12DeviceResources*     m_resources{ nullptr };
    ComPtr<ID3D12Device1>          m_device;
    ComPtr<ID3D12PipelineLibrary>  m_library;
    // 라이브러리는 넘겨받은 블롭을 복사하지 않는다 — 라이브러리보다 오래 살려야 한다.
    std::vector<uint8_t>           m_libraryBlob;
    std::wstring                   m_cachePath;
    bool                           m_libraryLoadedFromDisk{ false };

    /// 같은 desc 를 두 번 물으면 **같은 핸들**이 나와야 한다 — 매번 발급하면
    /// 표가 무한히 자란다.
    struct CacheEntry
    {
        ComPtr<ID3D12PipelineState> pso;
        RHIPipelineHandle           handle;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, CacheEntry> m_cache;
    std::unordered_map<uint64_t, std::shared_future<ComPtr<ID3D12PipelineState>>> m_pending;
    ComPtr<ID3D12PipelineState> m_fallback;
    Stats m_stats;
};

#endif
