#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/RHIResourceTypes.h"
#include "Texture.h"
#include "PathFinder.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// 텍스처 코덱 경계 — DX12/Vulkan 업로드 A/B 다이제스트 대조.
//
// ── 이 검사가 지키는 것 ──
//
// 두 백엔드가 같은 자산을 **같은 픽셀로** 올리는가. 포맷이 보존되는지는
// vk.gizmoicon · dx12.gizmoicon 의 코덱 슬라이스가 이미 묻는다. 여기서 묻는
// 것은 그 다음 질문이다 — 포맷이 같아도 배치 계산이 어긋나면 그림이 밀린다.
//
// ★ 왜 동어반복이 아닌가. 두 백엔드는 목적지 배치를 **각자 유도한다**.
//   DX12 는 GetCopyableFootprints 로 드라이버에게 묻고(256바이트 정렬),
//   Vulkan 은 RHIFormatRowPitch 로 직접 계산한다(빈틈 없음). 다이제스트는
//   행마다 유효 바이트만 먹으므로 패딩 차이는 지워지고 "같은 픽셀을 같은
//   순서로 올렸는가"만 남는다. 한쪽 계산이 틀어지면 값이 갈린다.
//
//   블록 압축이 들어오면서 그 계산이 블록 단위로 바뀌었고(BC1 은 폭 4텍셀에
//   8바이트, BC3 은 16바이트), 이 대조가 지키는 자리가 정확히 거기다.
//
// ★ 못 잡는 것(문서화까지가 증명). 소스(TextureImageView)가 틀리면 두
//   백엔드가 사이좋게 같은 쓰레기를 올려 값이 일치한다. 그 축은 DX12 픽셀
//   골든이 담당한다 — dx12.gizmoicon 이 뷰 포인터를 16행 미는 변이를 실제로
//   잡는 것을 확인했다(같은 변이에 vk.gizmoicon 은 눈멀었다).
namespace
{
    constexpr uint32_t kCodecWindow = 64;

    struct CodecAssets
    {
        std::shared_ptr<Texture> plain;       // RGBA8 또는 BGRA8 — 실제 PNG
        std::shared_ptr<Texture> compressed;  // BC1_UNORM_SRGB — baseColor 압축 경로
        std::shared_ptr<Texture> blockNoise;  // BC3_UNORM — blueNoise.dds
        std::shared_ptr<Texture> bgra;        // BGRA8_UNORM — 합성

        bool IsValid() const
        {
            return plain && compressed && blockNoise && bgra;
        }
    };

    /// 저작 경로가 실제로 만드는 것들이다. baseColorMap 은 compress=true 로
    /// BC1 이 되고(FinalizeMaterialRuntime), blueNoise.dds 는 DXT5 이며,
    /// WIC PNG 디코더는 FORCE_RGB 가 없으면 흔히 BGRA8 을 남긴다.
    bool BuildCodecAssets(CodecAssets& out, std::string& outError)
    {
        const file::path iconPath = PathFinder::IconPath() / L"CameraGizmo.png";
        out.plain = Texture::LoadSharedFromPath(iconPath);
        out.compressed = Texture::LoadSharedFromPath(iconPath, /*isCompress*/ true);
        out.blockNoise = Texture::LoadSharedFromPath(
            PathFinder::Relative("VolumetricFog\\blueNoise.dds"));

        const uint8_t bgraPixel[4] = { 32u, 64u, 128u, 255u };   // B, G, R, A
        out.bgra.reset(Texture::CreateFromPixels(
            1, 1, "codec_ab_bgra", RHIFormat::BGRA8Unorm, bgraPixel));

        if (!out.IsValid())
        {
            outError = "코덱 자산 셋을 로드하지 못했다";
            return false;
        }

        // ★ 포맷을 먼저 단정한다. 로더가 압축을 안 했거나 dds 가 다른 포맷이면
        //   아래 대조는 비압축 자산 둘을 비교해 놓고 초록을 내는 눈먼 검사가
        //   된다 — 블록 배치를 재는 것이 이 검사의 존재 이유다.
        if (RHIFormat::BC1UnormSrgb != out.compressed->GetImageView().Format())
        {
            outError = "baseColor 압축 경로가 BC1_SRGB를 만들지 않았다";
            return false;
        }
        if (RHIFormat::BC3Unorm != out.blockNoise->GetImageView().Format())
        {
            outError = "blueNoise.dds가 BC3로 로드되지 않았다";
            return false;
        }
        if (RHIFormat::BGRA8Unorm != out.bgra->GetImageView().Format())
        {
            outError = "BGRA8 자산을 만들지 못했다";
            return false;
        }
        if (out.plain->GetImageView().IsEmpty())
        {
            outError = "비압축 자산을 로드하지 못했다";
            return false;
        }
        return true;
    }

    /// 올리는 순서를 한 곳에 둔다 — 두 백엔드가 반드시 같은 순서로 먹어야
    /// 누적 다이제스트가 비교 가능하다.
    template <typename CacheT>
    bool UploadAll(CacheT& cache, const CodecAssets& assets, std::string& outError)
    {
        Texture* const order[] = {
            assets.plain.get(), assets.compressed.get(),
            assets.blockNoise.get(), assets.bgra.get() };
        for (Texture* texture : order)
        {
            std::string error;
            const auto entry = cache.GetOrUpload(texture, error);
            if (!entry.IsValid() || entry.format != texture->GetImageView().Format())
            {
                outError = "업로드가 포맷을 지키지 못했다(" + texture->m_name + "): " + error;
                return false;
            }
        }
        return true;
    }

    bool CaptureDx12(const CodecAssets& assets, RHIUploadDigest& outDigest,
        uint32_t& outUploads, std::string& outError)
    {
        DX12DeviceResources resources;
        DX12TextureCache textures;
        if (!resources.Initialize(kCodecWindow, kCodecWindow, outError)) return false;
        if (!textures.Initialize(&resources, outError))
        {
            resources.Shutdown();
            return false;
        }

        bool ok = resources.BeginFrame(outError);
        if (ok)
        {
            textures.BeginFrame(0);
            ok = UploadAll(textures, assets, outError);
            std::string frameError;
            if (!resources.EndFrame(frameError) && ok)
            {
                outError = frameError;
                ok = false;
            }
            resources.WaitForGpu();
        }

        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        if (0 != problems && ok)
        {
            outError = "DX12 검증 레이어 " + std::to_string(problems) + "건\n" + validation;
            ok = false;
        }

        const DX12TextureCache::Stats stats = textures.GetStats();
        outDigest = stats.uploadDigest;
        outUploads = stats.uploads;
        if (ok && 0 != stats.failures)
        {
            outError = "DX12 업로드 실패 " + std::to_string(stats.failures) + "건";
            ok = false;
        }

        textures.Shutdown();
        resources.Shutdown();
        return ok;
    }

    bool CaptureVulkan(const CodecAssets& assets, RHIUploadDigest& outDigest,
        uint32_t& outUploads, std::string& outError)
    {
        VulkanDeviceResources resources;
        VulkanTextureCache textures;
        if (!VulkanApi::LoadLoader(outError)) return false;
        if (!resources.Initialize(kCodecWindow, kCodecWindow, true, outError)) return false;
        if (!textures.Initialize(&resources, outError))
        {
            resources.Shutdown();
            return false;
        }

        bool ok = resources.BeginFrame(outError);
        if (ok)
        {
            textures.BeginFrame(0);
            ok = UploadAll(textures, assets, outError);
            std::string frameError;
            if (!resources.EndFrame(frameError) && ok)
            {
                outError = frameError;
                ok = false;
            }
            resources.WaitForGpu();
        }

        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        if (0 != problems && ok)
        {
            outError = "Vulkan 검증 레이어 " + std::to_string(problems) + "건\n" + validation;
            ok = false;
        }

        const VulkanTextureCache::Stats stats = textures.GetStats();
        outDigest = stats.uploadDigest;
        outUploads = stats.uploads;
        if (ok && 0 != stats.failures)
        {
            outError = "Vulkan 업로드 실패 " + std::to_string(stats.failures) + "건";
            ok = false;
        }

        textures.Shutdown();
        resources.Shutdown();
        return ok;
    }
}

bool RunVulkanTextureCodecTest(std::string& outLog)
{
    outLog += "── 텍스처 코덱 경계 — DX12/Vulkan 업로드 A/B 다이제스트 ──\n";

    CodecAssets assets;
    std::string error;
    if (!BuildCodecAssets(assets, error))
    {
        outLog += "[1/3] " + error + "\n";
        return false;
    }
    char assetLine[256]{};
    std::snprintf(assetLine, sizeof(assetLine),
        "[1/3] 자산 넷 준비 — plain %u · BC1_SRGB %u · BC3 %u · BGRA8 %u (서브리소스)\n",
        assets.plain->GetImageView().SubresourceCount(),
        assets.compressed->GetImageView().SubresourceCount(),
        assets.blockNoise->GetImageView().SubresourceCount(),
        assets.bgra->GetImageView().SubresourceCount());
    outLog += assetLine;

    // 스위치는 이 검사 안에서만 켠다 — 4K HDR 128MB 를 매번 해시하면 로드가
    // 눈에 띄게 느려지므로 런타임 기본값은 off 다.
    SetRHIUploadDigestEnabled(true);
    struct DigestScope { ~DigestScope() { SetRHIUploadDigestEnabled(false); } } scope;

    RHIUploadDigest dx12Digest;
    uint32_t dx12Uploads = 0;
    if (!CaptureDx12(assets, dx12Digest, dx12Uploads, error))
    {
        outLog += "[2/3] DX12 캡처 실패: " + error + "\n";
        return false;
    }

    RHIUploadDigest vulkanDigest;
    uint32_t vulkanUploads = 0;
    if (!CaptureVulkan(assets, vulkanDigest, vulkanUploads, error))
    {
        outLog += "[2/3] Vulkan 캡처 실패: " + error + "\n";
        return false;
    }

    char captureLine[320]{};
    std::snprintf(captureLine, sizeof(captureLine),
        "[2/3] 업로드 DX12 %u / Vulkan %u · 논리 바이트 %llu / %llu · 행 %u / %u\n",
        dx12Uploads, vulkanUploads,
        static_cast<unsigned long long>(dx12Digest.bytes),
        static_cast<unsigned long long>(vulkanDigest.bytes),
        dx12Digest.rows, vulkanDigest.rows);
    outLog += captureLine;

    // 빈 다이제스트는 "아무것도 안 쟀다"이지 일치가 아니다. 스위치가 꺼진 채
    // 돌면 양쪽이 사이좋게 초기값이라 == 가 참이 된다 — 그 거짓 통과를 막는다.
    if (dx12Digest.IsEmpty() || vulkanDigest.IsEmpty())
    {
        outLog += "[3/3] 다이제스트가 비었다 — 수집 스위치가 배선되지 않았다\n";
        return false;
    }
    if (4 != dx12Uploads || 4 != vulkanUploads)
    {
        outLog += "[3/3] 자산 넷이 각 백엔드에서 한 번씩 올라가지 않았다\n";
        return false;
    }

    char digestLine[256]{};
    std::snprintf(digestLine, sizeof(digestLine),
        "[3/3] 다이제스트 DX12 %016llx / Vulkan %016llx\n",
        static_cast<unsigned long long>(dx12Digest.hash),
        static_cast<unsigned long long>(vulkanDigest.hash));
    outLog += digestLine;

    if (dx12Digest != vulkanDigest)
    {
        outLog += "두 백엔드가 같은 자산을 다른 픽셀로 올렸다\n";
        return false;
    }

    outLog += "텍스처 코덱 경계 검증 통과 — BC1·BC3·BGRA8·비압축 넷이 "
        "두 백엔드에서 같은 논리 픽셀\n";
    return true;
}
