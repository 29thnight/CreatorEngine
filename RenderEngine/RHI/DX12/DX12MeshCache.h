#pragma once
#ifndef DYNAMICCPP_EXPORTS
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
class DX12MeshCache
{
public:
    struct Entry
    {
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW  indexView{};
        uint32_t                 indexCount{ 0 };

        bool IsValid() const { return 0 != indexCount; }
    };

    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t uploads{ 0 };
        uint32_t failures{ 0 };
        uint64_t bytesUploaded{ 0 };
    };

    bool Initialize(DX12DeviceResources* resources, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_resources; }

    /// 메시를 올리고 뷰를 돌려준다. 이미 올라가 있으면 그대로 준다.
    ///
    /// 업로드는 커맨드 리스트 기록을 동반하므로 프레임이 열려 있어야 한다
    /// (BeginFrame과 EndFrame 사이). 패스 기록 중에 부르면 안 된다 —
    /// Record는 리소스를 만들지 않는다는 3-6의 규약을 어기는 것이다.
    Entry GetOrUpload(Mesh* mesh, std::string& outError);

    Stats  GetStats() const { return m_stats; }
    size_t GetCachedCount() const { return m_entries.size(); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Buffers
    {
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        Entry entry;
    };

    bool UploadBuffer(const void* data, uint64_t bytes, D3D12_RESOURCE_STATES finalState,
        ComPtr<ID3D12Resource>& outBuffer, const wchar_t* name, std::string& outError);

    DX12DeviceResources* m_resources{ nullptr };
    std::unordered_map<Mesh*, Buffers> m_entries;
    Stats m_stats;
};

#endif
