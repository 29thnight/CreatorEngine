#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <vector>
#include <future>
#include <mutex>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>

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

// 파이프라인 상태의 해시 대상. 셰이더는 내용으로 해시하므로 셰이더가 바뀌면
// 자동으로 다른 키가 된다 — 핫리로드와 캐시 무효화가 같은 메커니즘으로 처리된다.
struct DX12GraphicsPipelineDesc
{
    const void* vsBytecode{ nullptr };
    size_t      vsSize{ 0 };
    const void* psBytecode{ nullptr };
    size_t      psSize{ 0 };

    // 루트 시그니처는 포인터가 실행마다 달라 해시에 못 쓴다. 호출부가 안정된
    // 식별자(이름 해시 등)를 주고, 실제 포인터는 생성에만 쓴다.
    ID3D12RootSignature* rootSignature{ nullptr };
    uint64_t             rootSignatureId{ 0 };

    D3D12_FILL_MODE fillMode{ D3D12_FILL_MODE_SOLID };
    D3D12_CULL_MODE cullMode{ D3D12_CULL_MODE_NONE };
    bool            depthEnable{ false };
    bool            blendEnable{ false };

    // 입력 레이아웃. 호출부가 소유하고, 이 구조체는 참조만 든다 —
    // GetOrCreate가 돌아올 때까지만 살아 있으면 된다.
    //
    // 해시는 반드시 내용으로 해야 한다. D3D12_INPUT_ELEMENT_DESC의 SemanticName은
    // const char*라 통째로 바이트 해시하면 문자열 주소를 해시하게 되고, 같은
    // 레이아웃이 매번 다른 키가 되어 캐시가 논다(루트 시그니처에서 겪은 것과 같다).
    const D3D12_INPUT_ELEMENT_DESC* inputElements{ nullptr };
    uint32_t                        inputElementCount{ 0 };

    D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType{ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE };
    uint32_t    numRenderTargets{ 1 };
    DXGI_FORMAT rtvFormats[8]{ DXGI_FORMAT_R8G8B8A8_UNORM };
    DXGI_FORMAT dsvFormat{ DXGI_FORMAT_UNKNOWN };
    uint32_t    sampleCount{ 1 };

    uint64_t ComputeHash() const;
};

// 컴퓨트 파이프라인. 그래픽과 같은 해시 공간을 쓰되 태그로 구분해, 우연히 같은
// 바이트 배열이 나와도 두 종류가 섞이지 않게 한다.
struct DX12ComputePipelineDesc
{
    const void* csBytecode{ nullptr };
    size_t      csSize{ 0 };
    ID3D12RootSignature* rootSignature{ nullptr };
    uint64_t             rootSignatureId{ 0 };

    uint64_t ComputeHash() const;
};

class DX12PSOManager
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
    };

    // 비동기 요청의 상태. Pending 프레임은 폴백 PSO를 쓰거나 그 드로우를 건너뛴다 —
    // 어느 쪽이든 컴파일 스톨이 프레임에 들어오지 않는 것이 목적이다.
    enum class RequestState { Ready, Pending, Failed };

    bool Initialize(ID3D12Device* device, const std::wstring& cacheFilePath, std::string& outError);
    void Shutdown();

    // 동기 취득 — 미스면 이 자리에서 컴파일한다(로딩 시점용).
    ID3D12PipelineState* GetOrCreate(const DX12GraphicsPipelineDesc& desc, std::string& outError);

    // 컴퓨트 PSO. 같은 캐시 2층을 공유한다.
    ID3D12PipelineState* GetOrCreateCompute(const DX12ComputePipelineDesc& desc, std::string& outError);

    // 비동기 요청 — 준비됐으면 Ready + 포인터, 아니면 Pending(컴파일은 백그라운드로).
    RequestState Request(const DX12GraphicsPipelineDesc& desc, ID3D12PipelineState** outPso);

    // ── 폴백 정책 ──
    //
    // 비동기 컴파일이 끝나지 않은 프레임에 무엇을 그릴지의 답이다. 폴백을 등록해
    // 두면 Pending 대신 폴백 PSO가 나와 그 드로우를 계속 그릴 수 있고(보통 단순
    // 셰이더), 등록하지 않았으면 호출부가 그 드로우를 건너뛴다.
    //
    // 어느 쪽이든 프레임이 컴파일을 기다리지 않는 것이 핵심이다.
    bool SetFallback(const DX12GraphicsPipelineDesc& desc, std::string& outError);

    enum class DrawDecision { UseRequested, UseFallback, Skip };

    // 프레임 기록 경로에서 쓰는 형태. 요청이 준비됐으면 그것을, 아니면 폴백을,
    // 폴백도 없으면 Skip을 돌려준다.
    DrawDecision Resolve(const DX12GraphicsPipelineDesc& desc, ID3D12PipelineState** outPso);

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
    ComPtr<ID3D12PipelineState> CreateOne(const DX12GraphicsPipelineDesc& desc,
        uint64_t hash, std::string& outError);
    ComPtr<ID3D12PipelineState> CreateOneCompute(const DX12ComputePipelineDesc& desc,
        uint64_t hash, std::string& outError);

    ComPtr<ID3D12Device1>          m_device;
    ComPtr<ID3D12PipelineLibrary>  m_library;
    // 라이브러리는 넘겨받은 블롭을 복사하지 않는다 — 라이브러리보다 오래 살려야 한다.
    std::vector<uint8_t>           m_libraryBlob;
    std::wstring                   m_cachePath;
    bool                           m_libraryLoadedFromDisk{ false };

    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, ComPtr<ID3D12PipelineState>> m_cache;
    std::unordered_map<uint64_t, std::shared_future<ComPtr<ID3D12PipelineState>>> m_pending;
    ComPtr<ID3D12PipelineState> m_fallback;
    Stats m_stats;
};

#endif
