#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>
#include <d3d11.h>

class Texture;
class DX12DeviceResources;

// 씬 텍스처를 DX12로 옮겨 두는 캐시 (PHASE 3-6, 재질 연결).
//
// 메시와 달리 텍스처는 CPU 사본이 없다. Texture는 ID3D11Texture2D와 SRV만 들고
// 있고 원본 픽셀은 로드 직후 버려진다. 그래서 세 가지 중 하나를 골라야 했다:
//
//   ① 파일에서 다시 로드 — 경로가 필요한데 Texture가 드는 것은 이름이지 경로가
//      아니고, 런타임에 만든 텍스처(렌더 타깃 등)에는 파일 자체가 없다.
//   ② DX11 텍스처를 공유 — 이미 만들어진 텍스처는 공유 플래그 없이 생성됐다.
//      전부 다시 만들어야 하는데 그건 DX11 쪽에 손대는 일이라 3-10 원칙에 어긋난다.
//   ③ DX11에서 읽어 DX12로 올린다 — 느리지만 텍스처당 한 번이고, 출처를 가리지
//      않으며, DX11 쪽을 건드리지 않는다.
//
// ③을 골랐다. 병존 기간 전용이라는 점도 같다 — 교체(3-9) 후에는 텍스처가 처음부터
// DX12로 로드되므로 이 경로는 공유 텍스처 경로와 함께 사라진다.
//
// 비용은 명시해 둔다: DX11 스테이징 복사 → Map → 업로드 링 → CopyTextureRegion.
// 텍스처당 한 번이지만 큰 텍스처는 수십 ms가 들 수 있어 프레임 중에 부르면 안 된다.
// 로딩 시점이나 첫 프레임에 몰아서 올리는 것이 전제다.
class DX12TextureCache
{
public:
    struct Entry
    {
        ID3D12Resource* resource{ nullptr };
        DXGI_FORMAT     format{ DXGI_FORMAT_UNKNOWN };
        uint32_t        width{ 0 };
        uint32_t        height{ 0 };
        uint32_t        mipLevels{ 0 };

        bool IsValid() const { return nullptr != resource; }
    };

    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t uploads{ 0 };
        uint32_t failures{ 0 };
        uint64_t bytesUploaded{ 0 };
    };

    bool Initialize(DX12DeviceResources* resources, ID3D11Device* dx11Device,
        ID3D11DeviceContext* dx11Context, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_resources; }

    /// 텍스처를 올리고 핸들을 돌려준다. 이미 올라가 있으면 그대로 준다.
    /// 프레임이 열려 있어야 한다(BeginFrame과 EndFrame 사이).
    Entry GetOrUpload(Texture* texture, std::string& outError);

    /// 재질에 텍스처가 없을 때 쓸 1x1 흰색. 분기 없이 항상 뭔가를 바인딩할 수
    /// 있게 해 준다 — 셰이더에서 "텍스처가 있으면" 분기를 없애는 쪽이 빠르다.
    Entry GetWhiteTexture() const { return m_white; }

    Stats  GetStats() const { return m_stats; }
    size_t GetCachedCount() const { return m_entries.size(); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreateWhiteTexture(std::string& outError);
    bool UploadFromDX11(ID3D11Texture2D* source, const D3D11_TEXTURE2D_DESC& desc,
        ComPtr<ID3D12Resource>& outResource, std::string& outError);

    DX12DeviceResources* m_resources{ nullptr };
    ID3D11Device*        m_dx11Device{ nullptr };
    ID3D11DeviceContext* m_dx11Context{ nullptr };

    std::unordered_map<Texture*, ComPtr<ID3D12Resource>> m_entries;
    std::unordered_map<Texture*, Entry> m_descriptions;

    ComPtr<ID3D12Resource> m_whiteResource;
    Entry m_white;

    Stats m_stats;
};

#endif
