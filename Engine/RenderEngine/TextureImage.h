#pragma once
#include "RHI/RHIFormat.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// 텍스처의 CPU 픽셀 — 백엔드 중립 (축 A).
//
// ── 왜 이 타입이 생겼나 ──
//
// 이 자리에 있던 것은 DirectX::ScratchImage 였다. 자산(Texture)이 그것을
// 들고, DX12·Vulkan 텍스처 캐시가 둘 다 그것을 읽었다. 그런데 소비 지점이
// 실제로 묻는 것은 여섯 가지뿐이다 — 포맷·치수·밉 수·배열 수, 그리고
// 서브리소스마다 픽셀 포인터와 행 간격. 그 여섯을 얻으려고 Texture.h 가
// <DirectXTex.h> 를, 그리고 그것이 요구하는 <d3d11.h> 를 모든 소비자에게
// 퍼뜨렸다. Vulkan 백엔드 구현 파일이 d3d11.h 를 컴파일하고 있었다.
//
// ── 왜 뷰와 컨테이너가 둘인가 ──
//
// ★ 처음에는 소유 컨테이너 하나로 만들었다가 되돌렸다. 디코더가 낸 픽셀을
//   컨테이너로 옮기려면 전량 복사가 한 번 필요한데, 실측하니 4K HDR
//   equirect 한 장이 128MB 였다(4096x2048 RGBA32F, 저장소에 19장).
//   스카이박스를 바꿀 때마다 128MB memcpy 가 붙는다. PNG 는 평균 844KB 라
//   무시할 만했지만 HDR 이 그 결정을 뒤집었다.
//
//   그래서 경계를 넘는 것은 **뷰**다. 디코더가 낸 픽셀을 그 자리에 두고
//   가리키기만 하므로 복사가 0이다.
//
//   소유 컨테이너(TextureImage)는 남는다. cook 경로처럼 "이미 중립인 픽셀을
//   받아 들어야" 하는 자리가 있고, 그때 뷰가 가리킬 실체가 필요하다.
//
// ★ 수명 규약 — 뷰는 소유자보다 오래 살 수 없다. 지금 소비자 둘(DX12·Vulkan
//   텍스처 캐시)은 GetOrUpload 안에서 뷰를 받아 그 함수 안에서 다 쓰고
//   버린다. 뷰를 멤버로 들거나 프레임을 넘기면 그 순간 규약이 깨진다.
//   컨테이너 방식에는 없던 위험이고, 복사를 없앤 대가다.
//
// ★ 디코더(WIC·DDS·TGA·HDR)와 BC 압축기는 여전히 DirectXTex 가 한다. 그것이
//   DirectXTex 를 못 걷는 진짜 이유이고, 이 타입이 답할 문제가 아니다.
//   지금 그쪽은 Texture.cpp 안에만 있다 — 교체하려면 그 파일 하나만 보면 된다.
//
// ── 왜 RHI 폴더가 아닌가 ──
//
// ★ 처음에는 이름이 RHICpuImage 였고 RHI/ 아래 있었다. 둘 다 거짓말이다.
//   이것은 GPU 리소스도 백엔드 추상화도 아니라 **GPU 에 올라가기 전의 자산
//   데이터**다. RHIFormat 에 의존한다는 것은 그 폴더에 둘 이유가 못 된다 —
//   assets::ModelTextureAsset::format 도 이미 RHIFormat 이다. 포맷 어휘는
//   백엔드 중립이라 자산 쪽에서도 쓰는 것이 정상이다.
//
// ── 레이아웃 규약 ──
//
// 서브리소스는 item 바깥, mip 안쪽으로 늘어놓는다(index = item * mipLevels
// + mip). ModelAssetGeneration 의 적재 순서와 같고, DX12 의 서브리소스 번호
// 규칙(mip + arraySlice * mipLevels)과도 같다 — 세 곳이 같은 식을 쓰도록
// 맞춘 것이다.

/// 서브리소스 하나(밉 하나 x 배열 원소 하나)의 CPU 픽셀. 소유하지 않는다.
struct TextureSubimage
{
    const std::byte* pixels{ nullptr };
    size_t   rowPitch{ 0 };    ///< 압축이면 '블록 한 줄'의 바이트 수
    size_t   slicePitch{ 0 };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};

/// 행 단위 복사. 소스와 대상의 행 간격이 다른 것이 기본이라(정렬 요구가
/// 서로 다르다) 통짜 memcpy 를 쓰면 그림이 한 행씩 밀려 비스듬해진다 —
/// 1x1 에서는 안 드러나고 폭이 커지는 순간 나타나는 부류다.
inline void CopyImageRows(std::byte* destination, size_t destinationPitch,
    const std::byte* source, size_t sourcePitch, uint32_t rows, size_t rowBytes)
{
    const size_t copyBytes = (std::min)(rowBytes, (std::min)(destinationPitch, sourcePitch));
    for (uint32_t row = 0; row < rows; ++row)
    {
        std::memcpy(destination + static_cast<size_t>(row) * destinationPitch,
            source + static_cast<size_t>(row) * sourcePitch, copyBytes);
    }
}

/// CPU 이미지의 읽기 전용 뷰. **소유하지 않는다** — 수명은 소유자가 정한다.
///
/// RHI 경계를 넘는 것은 이 타입이다. 값으로 들고 다녀도 싼 크기지만(포인터
/// 하나 + 서술자), 가리키는 픽셀보다 오래 살면 안 된다.
class TextureImageView
{
public:
    TextureImageView() = default;

    TextureImageView(RHIFormat format, uint32_t width, uint32_t height,
        uint32_t mipLevels, uint32_t arraySize, bool isCube,
        const TextureSubimage* subresources, uint32_t subresourceCount)
        : m_format(format), m_width(width), m_height(height)
        , m_mipLevels(mipLevels), m_arraySize(arraySize), m_isCube(isCube)
        , m_subresources(subresources), m_subresourceCount(subresourceCount)
    {
    }

    [[nodiscard]] bool IsEmpty() const
    {
        return RHIFormat::Unknown == m_format || nullptr == m_subresources
            || 0 == m_subresourceCount;
    }

    [[nodiscard]] RHIFormat Format() const { return m_format; }
    [[nodiscard]] uint32_t  Width() const { return m_width; }
    [[nodiscard]] uint32_t  Height() const { return m_height; }
    [[nodiscard]] uint32_t  MipLevels() const { return m_mipLevels; }
    [[nodiscard]] uint32_t  ArraySize() const { return m_arraySize; }
    [[nodiscard]] bool      IsCube() const { return m_isCube; }
    [[nodiscard]] uint32_t  SubresourceCount() const { return m_subresourceCount; }

    /// 늘어놓은 순서 그대로. DX12 서브리소스 번호와 같은 순서다.
    [[nodiscard]] const TextureSubimage* At(uint32_t index) const
    {
        if (index >= m_subresourceCount) return nullptr;
        return m_subresources + index;
    }

    /// (밉, 배열 원소)로 찾는다. 범위 밖이면 nullptr.
    [[nodiscard]] const TextureSubimage* Find(uint32_t mip, uint32_t item) const
    {
        if (mip >= m_mipLevels || item >= m_arraySize) return nullptr;
        return At(item * m_mipLevels + mip);
    }

private:
    RHIFormat m_format{ RHIFormat::Unknown };
    uint32_t  m_width{ 0 };
    uint32_t  m_height{ 0 };
    uint32_t  m_mipLevels{ 0 };
    uint32_t  m_arraySize{ 0 };
    bool      m_isCube{ false };

    const TextureSubimage* m_subresources{ nullptr };
    uint32_t               m_subresourceCount{ 0 };
};

/// 픽셀을 소유하는 CPU 이미지.
///
/// 파일 디코드 경로는 이것을 쓰지 않는다 — 디코더가 낸 픽셀을 그 자리에 두고
/// 뷰로 가리키는 것이 요점이기 때문이다(위 128MB 실측). 이 타입이 필요한
/// 자리는 "이미 중립인 픽셀을 받아 들어야" 하는 쪽이다: cooked generation 의
/// 텍스처, 그리고 자가 검증이 세우는 합성 픽셀.
///
/// 복사를 막는다 — 서브리소스가 자기 픽셀 버퍼 안을 가리키므로 복사하면 그
/// 포인터가 원본을 가리킨 채 남는다. 이동은 안전하다(vector 이동은 버퍼
/// 주소를 보존한다).
class TextureImage
{
public:
    TextureImage() = default;
    TextureImage(const TextureImage&) = delete;
    TextureImage& operator=(const TextureImage&) = delete;
    TextureImage(TextureImage&&) noexcept = default;
    TextureImage& operator=(TextureImage&&) noexcept = default;

    /// 밉·배열 체인의 자리를 잡고 픽셀 버퍼를 0으로 채운다.
    ///
    /// 실패하면 빈 이미지를 돌려준다(IsValid() 가 false). 포맷을 모르거나
    /// 치수가 0이면 실패다 — 부분적으로 채워진 이미지를 만들지 않는다.
    [[nodiscard]] static TextureImage Allocate(RHIFormat format, uint32_t width,
        uint32_t height, uint32_t arraySize, uint32_t mipLevels, bool isCube = false)
    {
        TextureImage image;
        if (RHIFormat::Unknown == format || 0 == width || 0 == height
            || 0 == arraySize || 0 == mipLevels || 0 == RHIFormatBlockBytes(format))
            return image;

        image.m_format = format;
        image.m_width = width;
        image.m_height = height;
        image.m_arraySize = arraySize;
        image.m_mipLevels = mipLevels;
        image.m_isCube = isCube;

        // 자리를 두 번 돈다. 픽셀 버퍼를 먼저 확정해야 서브리소스가 그 안을
        // 가리킬 수 있고, vector 가 자라면서 주소가 바뀌는 일이 없어진다.
        std::vector<size_t> offsets;
        offsets.reserve(static_cast<size_t>(arraySize) * mipLevels);
        size_t total = 0;
        for (uint32_t item = 0; item < arraySize; ++item)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                const uint32_t mipWidth = (std::max)(1u, width >> mip);
                const uint32_t mipHeight = (std::max)(1u, height >> mip);
                offsets.push_back(total);
                total += static_cast<size_t>(
                    RHIFormatSlicePitch(format, mipWidth, mipHeight));
            }
        }
        image.m_pixels.assign(total, std::byte{ 0 });

        image.m_subresources.reserve(offsets.size());
        size_t index = 0;
        for (uint32_t item = 0; item < arraySize; ++item)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip, ++index)
            {
                const uint32_t mipWidth = (std::max)(1u, width >> mip);
                const uint32_t mipHeight = (std::max)(1u, height >> mip);
                TextureSubimage subresource;
                subresource.pixels = image.m_pixels.data() + offsets[index];
                subresource.rowPitch = static_cast<size_t>(
                    RHIFormatRowPitch(format, mipWidth));
                subresource.slicePitch = static_cast<size_t>(
                    RHIFormatSlicePitch(format, mipWidth, mipHeight));
                subresource.width = mipWidth;
                subresource.height = mipHeight;
                image.m_subresources.push_back(subresource);
            }
        }
        return image;
    }

    [[nodiscard]] bool IsValid() const
    {
        return RHIFormat::Unknown != m_format && !m_pixels.empty()
            && !m_subresources.empty();
    }

    [[nodiscard]] TextureImageView View() const
    {
        if (!IsValid()) return {};
        return TextureImageView(m_format, m_width, m_height, m_mipLevels,
            m_arraySize, m_isCube, m_subresources.data(),
            static_cast<uint32_t>(m_subresources.size()));
    }

    [[nodiscard]] RHIFormat Format() const { return m_format; }
    [[nodiscard]] uint32_t  Width() const { return m_width; }
    [[nodiscard]] uint32_t  Height() const { return m_height; }
    [[nodiscard]] uint32_t  MipLevels() const { return m_mipLevels; }
    [[nodiscard]] uint32_t  ArraySize() const { return m_arraySize; }
    [[nodiscard]] bool      IsCube() const { return m_isCube; }
    [[nodiscard]] size_t    TotalBytes() const { return m_pixels.size(); }

    [[nodiscard]] const TextureSubimage* Find(uint32_t mip, uint32_t item) const
    {
        if (mip >= m_mipLevels || item >= m_arraySize) return nullptr;
        return &m_subresources[static_cast<size_t>(item) * m_mipLevels + mip];
    }

    /// 채워 넣을 자리. Allocate 직후에만 쓴다.
    [[nodiscard]] std::byte* MutablePixelsAt(const TextureSubimage& subresource)
    {
        const std::byte* base = m_pixels.data();
        if (subresource.pixels < base
            || subresource.pixels + subresource.slicePitch > base + m_pixels.size())
            return nullptr;
        return m_pixels.data() + (subresource.pixels - base);
    }

private:
    RHIFormat m_format{ RHIFormat::Unknown };
    uint32_t  m_width{ 0 };
    uint32_t  m_height{ 0 };
    uint32_t  m_mipLevels{ 0 };
    uint32_t  m_arraySize{ 0 };
    bool      m_isCube{ false };

    std::vector<std::byte>       m_pixels;
    std::vector<TextureSubimage> m_subresources;
};
