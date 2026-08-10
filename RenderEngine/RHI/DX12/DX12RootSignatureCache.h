#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "RenderFrameServices.h"
#include "DX12ResourceEntries.h"
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>

// 루트 시그니처 중앙 관리 + 캐시 (PHASE 3-4).
//
// PSO 해시에는 루트 시그니처를 포인터로 넣을 수 없다 — 주소가 실행마다 달라
// 디스크 캐시의 키가 되지 못한다. 그래서 DX12GraphicsPipelineDesc는 호출부가
// 안정된 식별자(rootSignatureId)를 주기로 했는데, 지금은 그 값을 손으로 쓴다
// (자가 검증 코드에 1, 2, 10이 박혀 있다).
//
// 그게 조용한 함정이다. 두 패스가 같은 번호를 서로 다른 레이아웃에 붙이면
// PSO 캐시가 엉뚱한 파이프라인을 돌려준다. 증상은 화면이 틀리거나 죽는 것인데,
// 원인이 '캐시 히트'라 재현도 추적도 어렵다. 번호를 사람이 정하는 한 이 부류는
// 언젠가 반드시 일어난다.
//
// 설명(desc)을 해시해 식별자를 유도하면 그 부류가 성립하지 않는다.
// 같은 레이아웃이면 같은 id, 다른 레이아웃이면 다른 id를 구조가 보장한다.
// 덤으로 같은 레이아웃을 여러 패스가 쓸 때 루트 시그니처 객체도 하나로 준다.
//
// 해시는 반드시 '내용'으로 해야 한다. 설명이 파라미터와 정적 샘플러를 포인터
// (지금은 span)로 들고 있어서, 구조체를 통째로 바이트 해시하면 주소를 해시하는
// 꼴이 된다 — 같은 레이아웃이 매번 다른 키가 되고 캐시가 통째로 논다.
//
// V4: 받는 설명이 D3D12_ROOT_SIGNATURE_DESC 에서 RHIPipelineLayoutDesc 로
// 바뀌었다. 경계는 PHASE 3-4 에 이미 서 있었지만 그 경계를 지나가는 인자가
// DX12 라서, 인터페이스만 중립이고 내용은 그대로였다. DX12 구조체 조립이
// 이 파일 안으로 들어온다.
class DX12RootSignatureCache : public IRenderRootSignatureCache
{
public:
    // 정의는 DX12ResourceEntries.h로 옮겼다(인터페이스 순환 회피).
    // 기존 이름은 별칭으로 남긴다 — 호출부를 건드리지 않는다.
    using Entry = DX12RootSignatureEntry;

    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t creates{ 0 };
        uint32_t failures{ 0 };
    };

    bool Initialize(ID3D12Device* device, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_device.Get(); }

    // 같은 레이아웃이면 같은 객체와 같은 id를 돌려준다.
    Entry GetOrCreate(const RHIPipelineLayoutDesc& desc, std::string& outError) override;

    // 설명만으로 식별자를 구한다(생성 없이). 해시가 레이아웃에만 의존하는지
    // 검증할 때 쓴다.
    static uint64_t ComputeHash(const RHIPipelineLayoutDesc& desc);

    Stats  GetStats() const;
    size_t GetCachedCount() const;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D12Device> m_device;

    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, ComPtr<ID3D12RootSignature>> m_cache;
    Stats m_stats;
};

#endif
