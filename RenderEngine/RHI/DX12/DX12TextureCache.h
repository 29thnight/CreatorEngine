#pragma once
#ifndef DYNAMICCPP_EXPORTS
// HashedGuid 정의. 유니티 빌드가 전이로 공급하던 것이라 단독 빌드에서 드러났다.
// min/max 매크로를 이 구간에서만 걷어내는 이유는 DX12MeshCache.h의 같은 주석 참고.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "TypeTrait.h"
#pragma pop_macro("max")
#pragma pop_macro("min")

#include "../IRenderTextureCache.h"
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
//
// ★ 그래서 로더가 남긴 CPU 픽셀은 놓지 않는다(2026-08-08). 예전에는 첫
//   업로드 때 move로 가져왔는데, ③(미사용 은퇴)이 들어오면서 "은퇴 뒤
//   재업로드"가 생겼고 그때 픽셀이 비어 있어 실패했다. failures가 그것을
//   즉시 드러냈다 — ②의 관측이 값을 한 자리다.
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

        // ── 지금 들고 있는 양 (자산 상주 관리 ②) ──
        //
        // ★ bytesUploaded와 다르다. 그쪽은 누적이라 한 번 올린 것을 영원히
        //   센다 — "지금 VRAM을 얼마나 먹고 있나"를 답하지 못한다. 캐시가
        //   항목을 버리는 코드가 없다는 사실도 그 수로는 안 보였다.
        //
        // 판정: 씬 A → B → A를 왕복해 residentBytes가 기준선으로 돌아오는가.
        // ③(미사용 기반 은퇴)이 들어오기 전까지는 단조 증가가 정상이다 —
        // 그 증가폭이 곧 ③이 회수할 양이다.
        uint32_t residentCount{ 0 };
        uint64_t residentBytes{ 0 };

        // ── 은퇴 (자산 상주 관리 ③) ──
        //
        // retired는 누적(지금까지 몇 개를 놓았나), graveyard는 현재값
        // (펜스를 기다리는 중인 양)이다.
        uint32_t retired{ 0 };
        uint64_t retiredBytes{ 0 };
        uint32_t graveyardCount{ 0 };
        uint64_t graveyardBytes{ 0 };

        // 링 용량을 넘는 텍스처의 1회용 업로드 버퍼. ReleaseStagingBuffers가
        // 비우기 전까지 살아 있으므로 상주량에 포함해 봐야 한다 — 4K HDR
        // equirect 하나가 128MB였다(3-6 30차).
        uint32_t stagingCount{ 0 };
        uint64_t stagingBytes{ 0 };
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

    /// 대형 텍스처용 전용 스테이징을 즉시 비운다. GPU 유휴가 보장된 자리
    /// (WaitForGpu 뒤 · Shutdown)에서만 쓴다.
    ///
    /// ★ 평시에는 이것 대신 MarkStagingSubmitted + SweepStagingBuffers를 쓴다.
    ///   아래 주석 참고 — 이 함수만 두었더니 아무도 안 불러서 128MB가 종료까지
    ///   잡혀 있었다(실측).
    void ReleaseStagingBuffers()
    {
        m_dedicatedStaging.clear();
        m_stats.stagingCount = 0;
        m_stats.stagingBytes = 0;
    }

    // ── 스테이징 반납 (자산 상주 관리 ②-b) ──
    //
    // 링 용량(프레임당 16MB)을 넘는 텍스처는 1회용 업로드 버퍼로 나른다.
    // GPU가 복사를 끝내기 전에 파괴하면 안 되므로 캐시가 들고 있는다.
    //
    // ★ 처음에는 ReleaseStagingBuffers 하나만 두고 "GPU 유휴 시점에 부르라"고
    //   주석에 적어 두었다. 그런데 아무도 안 불렀다 — 실측에서 4K HDR
    //   equirect의 128MB가 종료까지 살아 있었고, 그것이 자산 상주 260MB의
    //   절반이었다. 규약을 주석에 적어 두는 것만으로는 지켜지지 않는다.
    //
    // 그래서 슬롯·묘지와 같은 형태로 바꾼다: 제출 펜스를 달고, 완료된 것만
    // 놓는다. 부르는 쪽은 "언제가 안전한가"를 몰라도 된다.
    //
    //   MarkStagingSubmitted(fence)  프레임 EndFrame 직후 — 이 프레임에 기록된
    //                                복사가 fence로 서명됐다고 알린다
    //   SweepStagingBuffers(done)    매 틱 — 완료된 것만 놓는다

    // ── 미사용 기반 은퇴 (자산 상주 관리 ③) ──
    //
    // 캐시가 항목을 버리는 코드가 하나도 없었다. 씬을 바꿔도 이전 씬의
    // 텍스처가 그대로 남고, DataSystem이 자기 참조를 놓아도(assets.unload)
    // 캐시는 통보를 못 받아 한 바이트도 안 돌아왔다(실측).
    //
    // ★ 왜 파괴자 통보가 아니라 미사용 기반인가 (AssetResidencyPlan §3.2)
    //
    //   ① 파괴 시점이 자유롭다 — 자산은 shared_ptr 공동 소유라 어느 스레드에서
    //      죽는지 정해져 있지 않다. 파괴자에서 렌더 캐시를 건드리면 Texture가
    //      렌더 계통을 알게 되고, 그건 T6이 걷으려는 방향과 반대다.
    //   ② 통보만으로는 부족하다 — 자산이 살아 있어도 씬에서 빠져 안 쓰이는
    //      경우가 훨씬 흔하다. 통보는 그것을 못 잡는다.
    //   ③ 미사용 기반은 통보를 포함한다 — 죽은 자산은 당연히 안 쓰인다.
    //
    // 통보는 회수를 *빠르게* 할 뿐이고 필요해지면 나중에 얹을 수 있다.

    /// 프레임 번호를 알린다. GetOrUpload가 이 값을 항목에 찍는다.
    void BeginFrame(uint64_t frameIndex) { m_frameIndex = frameIndex; }

    /// kRetireAfterFrames보다 오래 안 쓰인 항목을 묘지로 보낸다.
    /// 돌려줄 바이트를 반환한다(진단용).
    ///
    /// ★ 즉시 놓지 않는 이유: 인플라이트 제출이 아직 그 리소스를 참조할 수
    ///   있다. 임계값이 슬롯 수보다 크므로 구조적으로는 안전하지만, 슬롯·
    ///   스테이징과 같은 규약(펜스가 지나야 놓는다)을 쓰는 편이 낫다 —
    ///   규약이 하나면 나중에 임계값을 줄여도 안전이 유지된다.
    uint64_t RetireUnused(uint64_t fenceValue);

    /// 펜스가 지난 묘지를 비운다.
    uint64_t SweepGraveyard(uint64_t completedFenceValue);

    /// 이 프레임 수만큼 안 쓰이면 은퇴 대상이다.
    ///
    /// 슬롯 3 × 뷰 2보다 충분히 커야 한다 — 뷰마다 프레임이 어긋나므로
    /// 작게 잡으면 번갈아 쓰이는 텍스처가 은퇴와 재업로드를 반복한다.
    /// 씬 전환은 초 단위로 일어나므로 2초면 회수 목적에 충분하다.
    static constexpr uint64_t kRetireAfterFrames = 120;

    /// 아직 펜스가 안 달린 스테이징에 이번 제출의 펜스 값을 단다.
    void MarkStagingSubmitted(uint64_t fenceValue)
    {
        for (Staging& staging : m_dedicatedStaging)
        {
            if (0 == staging.fenceValue) staging.fenceValue = fenceValue;
        }
    }

    /// 완료된 스테이징을 놓는다. 돌려준 바이트를 반환한다(진단용).
    uint64_t SweepStagingBuffers(uint64_t completedFenceValue);

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

    // ── 키가 주소가 아니라 자산 신원이다 (자산 상주 관리 ①) ──
    //
    // 예전에는 Texture*였다. 자산 수명이 shared_ptr 공동 소유라 언제 어느
    // 스레드에서 죽는지 정해져 있지 않고, 죽은 뒤 같은 주소에 새 자산이
    // 올라오면 이 맵이 이전 것의 GPU 리소스를 돌려줬다 — 검증 레이어는
    // 조용하고 화면에만 '가끔 다른 텍스처'로 나타나는 부류다.
    //
    // ★ 신원 키가 누수를 고치지는 않는다. 죽은 자산의 항목은 여전히 남는다 —
    //   그것은 미사용 기반 은퇴(설계 ③)가 푼다. 여기서 없앤 것은 '틀린
    //   그림'이고, 남은 것은 '느려짐'이다. 순서가 그래서 이 쪽이 먼저다.
    /// 캐시가 든 리소스 하나. bytes는 ③(미사용 은퇴)이 뺄 때 쓴다 —
    /// 은퇴 시점에 desc로 다시 계산하면 생성 때와 어긋날 수 있어 그때 잰
    /// 값을 그대로 보관한다.
    struct Resident
    {
        ComPtr<ID3D12Resource> resource;
        uint64_t               bytes{ 0 };
        uint64_t               lastUsedFrame{ 0 };   // ③ — 은퇴 판정
    };

    std::unordered_map<HashedGuid, Resident> m_entries;
    std::unordered_map<HashedGuid, Entry> m_descriptions;

    /// 은퇴했으나 아직 GPU가 놓아 주지 않은 것들(③). 스테이징과 같은 규약이다.
    struct Grave
    {
        ComPtr<ID3D12Resource> resource;
        uint64_t               bytes{ 0 };
        uint64_t               fenceValue{ 0 };
    };
    std::vector<Grave> m_graveyard;

    uint64_t m_frameIndex{ 0 };

    ComPtr<ID3D12Resource> m_whiteResource;
    Entry m_white;
    ComPtr<ID3D12Resource> m_blackResource;
    Entry m_black;
    ComPtr<ID3D12Resource> m_ormNeutralResource;
    Entry m_ormNeutral;

    // 링 대신 쓴 1회용 업로드 버퍼들. GPU 소비가 끝날 때까지 살아 있어야 한다.
    //
    // fenceValue 0 = 아직 제출 전(이번 프레임에 기록 중). EndFrame 뒤
    // MarkStagingSubmitted가 값을 달고, SweepStagingBuffers가 완료를 보고 놓는다.
    struct Staging
    {
        ComPtr<ID3D12Resource> resource;
        uint64_t               bytes{ 0 };
        uint64_t               fenceValue{ 0 };
    };
    std::vector<Staging> m_dedicatedStaging;

    Stats m_stats;
};

#endif
