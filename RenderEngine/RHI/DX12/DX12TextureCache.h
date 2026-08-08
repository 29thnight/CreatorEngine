#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "RenderFrameServices.h"
#include "DX12ResourceEntries.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

// DirectXTex를 이 헤더로 끌어오지 않는다.
//
// 끌어오면 이 헤더를 먼저 include한 TU에서 DirectXTex.h가 d3d11.h보다 앞서게 되고,
// DirectXTex의 DX11 헬퍼(CreateShaderResourceView 등)가 __d3d11_h__ 가드에 걸려
// 통째로 사라진다. 뒤에 d3d11.h가 와도 #pragma once 때문에 되살아나지 않는다 —
// 실제로 ImGuiDx12Shell.cpp가 그렇게 깨졌다(PHASE 9-9).
// 선언에 필요한 것은 이름뿐이므로 전방 선언이면 충분하다.
namespace DirectX { class ScratchImage; }

class Texture;
class DX12DeviceResources;

// 씬 텍스처를 DX12로 옮겨 두는 캐시 (PHASE 3-6, 재질 연결).
//
// ── DX11 경유 폴백을 걷었다 (PHASE 3-1 재정의, T4) ──
//
// 처음에는 이 캐시가 DX11 텍스처에서 되읽었다. 그때 Texture는 픽셀을 들고
// 있지 않았기 때문이다 — 로더가 DX11 SRV를 만든 뒤 원본 이미지를 버렸다.
// 그래서 스테이징 복사 → Map → 업로드 링 → CopyTextureRegion을 거쳤다.
//
// T1이 그 전제를 없앴다. 로더가 압축까지 끝낸 이미지를 남겨 두므로
// (Texture::m_cpuPixels) 파일에서 온 텍스처는 전부 CPU 픽셀로 바로 올라간다.
// 실측 CPU 직결 8 · DX11 경유 0.
//
// ★ 폴백을 남기지 않는 이유: 도달할 수 없는 경로는 죽었는지 살았는지 알 수
//   없다. 지금 GetOrUpload를 부르는 곳 일곱은 전부 파일에서 읽은 텍스처를
//   넘긴다(재질·데칼·아이콘·UI·블루노이즈·스카이박스 equirect). 남겨 두면
//   DX11 디바이스 핸들 둘이 계속 인터페이스에 붙어 있고, 지형(T5)이 DX11로
//   만든 배열 텍스처를 그대로 밀어 넣는 손쉬운 길이 되어 T6을 막는다.
//
// CPU 픽셀이 없는 텍스처가 오면 흰색을 돌려주고 통계(failures)에 남긴다 —
// 조용히 다른 그림이 나오는 것보다 낫다.
class DX12TextureCache : public IRenderTextureCache
{
public:
    // 정의는 DX12ResourceEntries.h로 옮겼다(인터페이스 순환 회피).
    // 기존 이름은 별칭으로 남긴다 — 호출부를 건드리지 않는다.
    using Entry = DX12TextureEntry;

    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t uploads{ 0 };
        uint32_t failures{ 0 };
        uint64_t bytesUploaded{ 0 };

        // 업로드는 이제 한 경로뿐이다(T4로 DX11 폴백 제거). 값이 uploads와
        // 같아야 정상이고, 갈리면 어딘가 다른 경로가 생겼다는 뜻이다.
        uint32_t fromCpuPixels{ 0 };
    };

    bool Initialize(DX12DeviceResources* resources, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_resources; }

    /// 텍스처를 올리고 핸들을 돌려준다. 이미 올라가 있으면 그대로 준다.
    /// 프레임이 열려 있어야 한다(BeginFrame과 EndFrame 사이).
    Entry GetOrUpload(Texture* texture, std::string& outError) override;

    /// 재질에 텍스처가 없을 때 쓸 1x1 흰색. 분기 없이 항상 뭔가를 바인딩할 수
    /// 있게 해 준다 — 셰이더에서 "텍스처가 있으면" 분기를 없애는 쪽이 빠르다.
    Entry GetWhiteTexture() const { return m_white; }

    /// 슬롯 의미를 아는 폴백들. 흰색 하나로 전부 때우면 슬롯에 따라 뜻이
    /// 뒤집힌다 — emissive에 흰색이면 텍스처 없는 재질이 전부 자체발광이고,
    /// ORM에 흰색이면 B(금속)가 1이라 확산이 통째로 죽는다(IBL 소비 검증이
    /// '끔=검정' 대조군으로 잡았다). 프레임이 열려 있어야 한다(첫 호출 생성).
    Entry GetBlackTexture(std::string& outError) override;
    Entry GetOrmNeutralTexture(std::string& outError) override;   // (occ 1 · rough 1 · metal 0)

    /// 대형 텍스처용 전용 스테이징을 비운다. 링 용량을 넘는 텍스처(4K HDR
    /// equirect 128MB 실측)는 1회용 업로드 버퍼로 나르는데, GPU가 복사를
    /// 끝내기 전에 파괴하면 안 되므로 캐시가 들고 있는다 — GPU 유휴가
    /// 보장된 시점(WaitForGpu 뒤)에 이걸 불러 돌려준다. 안 부르면 Shutdown이
    /// 비운다(그때도 GPU 유휴가 계약이다).
    void ReleaseStagingBuffers() { m_dedicatedStaging.clear(); }

    Stats  GetStats() const { return m_stats; }
    size_t GetCachedCount() const { return m_entries.size(); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreateWhiteTexture(std::string& outError);
    bool CreateSolidTexture(const uint8_t rgba[4], const wchar_t* name,
        ComPtr<ID3D12Resource>& outResource, Entry& outEntry, std::string& outError);

    /// 파일에서 읽어 둔 CPU 픽셀로 올린다(T1). 이제 유일한 업로드 경로다.
    ///
    /// 로더가 압축까지 끝낸 이미지를 Texture에 남겨 두므로
    /// (Texture::m_cpuPixels) 그것을 그대로 업로드 링에 밀어 넣는다.
    bool UploadFromCpuPixels(const DirectX::ScratchImage& image,
        ComPtr<ID3D12Resource>& outResource, Entry& outEntry, std::string& outError);

    DX12DeviceResources* m_resources{ nullptr };

    std::unordered_map<Texture*, ComPtr<ID3D12Resource>> m_entries;
    std::unordered_map<Texture*, Entry> m_descriptions;

    ComPtr<ID3D12Resource> m_whiteResource;
    Entry m_white;
    ComPtr<ID3D12Resource> m_blackResource;
    Entry m_black;
    ComPtr<ID3D12Resource> m_ormNeutralResource;
    Entry m_ormNeutral;

    // 링 대신 쓴 1회용 업로드 버퍼들. GPU 소비가 끝날 때까지 살아 있어야 한다.
    std::vector<ComPtr<ID3D12Resource>> m_dedicatedStaging;

    Stats m_stats;
};

#endif
