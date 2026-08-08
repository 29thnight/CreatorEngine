#pragma once
#ifndef DYNAMICCPP_EXPORTS
// HashedGuid 정의. 유니티 빌드가 전이로 공급하던 것이라 단독 빌드에서 드러났다.
//
// TypeTrait.h는 boost/uuid를 함께 끌어오는데, boost가 쓰는
// std::numeric_limits<T>::max()가 windows.h의 min/max 매크로에 치환되어 깨진다
// (이 저장소는 NOMINMAX를 정의하지 않는다).
//
// 이 헤더 안에서 include 순서를 바꾸는 것으로는 풀리지 않는다 — 이 헤더를 쓰는
// .cpp가 이미 windows.h를 먼저 들여온 뒤에 여기 도달하기 때문이다. 그래서 순서가
// 아니라 매크로 자체를 이 구간에서만 걷어낸다. 원래 정의는 pop_macro로 되돌리므로
// 이 헤더 밖의 코드는 영향을 받지 않는다.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "TypeTrait.h"
#pragma pop_macro("max")
#pragma pop_macro("min")

#include "RenderFrameServices.h"
#include "DX12ResourceEntries.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>

class Mesh;
class DX12DeviceResources;

// 씬 메시를 DX12 버퍼로 올려 두는 캐시 (PHASE 3-6, 씬 연결).
//
// 엔진의 메시는 DX11 버퍼로 올라가 있어서 DX12가 그대로 쓸 수 없다. 다행히
// Mesh가 CPU 쪽 정점·인덱스 배열(m_vertices/m_indices)을 계속 들고 있어서
// 그것을 원본으로 DX12 버퍼를 만든다.
//
// 메시별로 한 번만 만든다. 프레임마다 만들면 업로드가 프레임 예산을 먹고,
// 그건 DX12로 옮긴 이유(CE 단계 단축)와 정반대다.
//
// DEFAULT 힙에 두고 업로드 링을 거쳐 복사한다. 업로드 힙에 두고 그대로 읽는
// 방법도 있지만(첫 슬라이스의 쿼드가 그랬다) 그건 PCIe를 매 드로우마다 타므로
// 실제 메시에는 맞지 않는다.
class DX12MeshCache : public IRenderMeshCache
{
public:
    // 정의는 DX12ResourceEntries.h로 옮겼다(인터페이스 순환 회피).
    // 기존 이름은 별칭으로 남긴다 — 호출부를 건드리지 않는다.
    using Entry = DX12MeshEntry;

    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t uploads{ 0 };
        uint32_t failures{ 0 };
        uint64_t bytesUploaded{ 0 };

        // ── 지금 들고 있는 양 (자산 상주 관리 ②) ──
        //
        // bytesUploaded는 누적이라 "지금 VRAM을 얼마나 먹고 있나"를 답하지
        // 못한다. 캐시가 항목을 버리는 코드가 없다는 사실도 그 수로는 안 보였다.
        // ③이 들어오기 전까지는 단조 증가가 정상이고, 그 증가폭이 곧 ③이
        // 회수할 양이다.
        uint32_t residentCount{ 0 };
        uint64_t residentBytes{ 0 };
    };

    bool Initialize(DX12DeviceResources* resources, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_resources; }

    /// 메시를 올리고 뷰를 돌려준다. 이미 올라가 있으면 그대로 준다.
    ///
    /// 업로드는 커맨드 리스트 기록을 동반하므로 프레임이 열려 있어야 한다
    /// (BeginFrame과 EndFrame 사이). 패스 기록 중에 부르면 안 된다 —
    /// Record는 리소스를 만들지 않는다는 3-6의 규약을 어기는 것이다.
    Entry GetOrUpload(Mesh* mesh, std::string& outError) override;

    Stats  GetStats() const { return m_stats; }
    size_t GetCachedCount() const { return m_entries.size(); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Buffers
    {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        Entry entry;

        /// 정점+인덱스 합계. ③(미사용 은퇴)이 뺄 때 쓴다 — 은퇴 시점에
        /// 다시 계산하지 않고 올릴 때 잰 값을 그대로 보관한다.
        uint64_t bytes{ 0 };
    };

    bool UploadBuffer(const void* data, uint64_t bytes, D3D12_RESOURCE_STATES finalState,
        ComPtr<ID3D12Resource>& outBuffer, const wchar_t* name, std::string& outError);

    DX12DeviceResources* m_resources{ nullptr };
    // ── 키가 주소가 아니라 자산 신원이다 (자산 상주 관리 ①) ──
    //
    // 텍스처 캐시와 같은 이유다. 자산 수명이 shared_ptr 공동 소유라 죽은 뒤
    // 같은 주소에 새 메시가 올라오면 이전 것의 정점 버퍼를 돌려줬다.
    // Mesh는 m_hashingMesh를 이미 들고 있어 새로 만들 것이 없었다.
    std::unordered_map<HashedGuid, Buffers> m_entries;
    Stats m_stats;
};

#endif
