#pragma once
#include "RHI/RHIFormat.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// 텍스처의 픽셀 — 백엔드 중립 (축 A).
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
// ★ 값을 두 번 뒤집던 자리가 그 대가를 가장 잘 보여 준다. cooked
//   generation 의 텍스처는 이미 RHIFormat + raw 픽셀로 중립인데,
//   DataSystem 이 그것을 DXGI_FORMAT 으로 되돌려 ScratchImage 로 재구성하고
//   (픽셀 전량 복사), Vulkan 캐시가 그 DXGI_FORMAT 을 다시 RHIFormat 으로
//   환원했다. 포맷 왕복 두 번이 순수한 어댑터 비용이었다.
//
// ★ 디코더(WIC·DDS·TGA·HDR)와 BC 압축기는 여전히 DirectXTex 가 한다. 그것이
//   DirectXTex 를 못 걷는 진짜 이유이고, 이 타입이 답할 문제가 아니다.
//   지금 그쪽은 Texture.cpp 안에 갇혀 있다 — 교체하려면 그 파일 하나만
//   보면 된다.
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

class TextureImage
{
public:
    /// 서브리소스 하나(밉 하나 x 배열 원소 하나)의 자리.
    struct Subresource
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        size_t   offset{ 0 };      ///< 픽셀 버퍼 안의 시작 위치
        size_t   rowPitch{ 0 };    ///< 압축이면 '블록 한 줄'의 바이트 수
        size_t   slicePitch{ 0 };
    };

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
        image.m_subresources.reserve(
            static_cast<size_t>(arraySize) * mipLevels);

        size_t offset = 0;
        for (uint32_t item = 0; item < arraySize; ++item)
        {
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                const uint32_t mipWidth = (std::max)(1u, width >> mip);
                const uint32_t mipHeight = (std::max)(1u, height >> mip);
                Subresource subresource;
                subresource.width = mipWidth;
                subresource.height = mipHeight;
                subresource.offset = offset;
                subresource.rowPitch = static_cast<size_t>(
                    RHIFormatRowPitch(format, mipWidth));
                subresource.slicePitch = static_cast<size_t>(
                    RHIFormatSlicePitch(format, mipWidth, mipHeight));
                offset += subresource.slicePitch;
                image.m_subresources.push_back(subresource);
            }
        }
        image.m_pixels.assign(offset, std::byte{ 0 });
        return image;
    }

    [[nodiscard]] bool IsValid() const
    {
        return RHIFormat::Unknown != m_format && !m_pixels.empty()
            && !m_subresources.empty();
    }

    [[nodiscard]] RHIFormat Format() const { return m_format; }
    [[nodiscard]] uint32_t  Width() const { return m_width; }
    [[nodiscard]] uint32_t  Height() const { return m_height; }
    [[nodiscard]] uint32_t  MipLevels() const { return m_mipLevels; }
    [[nodiscard]] uint32_t  ArraySize() const { return m_arraySize; }
    [[nodiscard]] bool      IsCube() const { return m_isCube; }
    [[nodiscard]] size_t    TotalBytes() const { return m_pixels.size(); }

    [[nodiscard]] uint32_t SubresourceCount() const
    {
        return static_cast<uint32_t>(m_subresources.size());
    }

    /// 서브리소스 자리. 범위 밖이면 nullptr.
    [[nodiscard]] const Subresource* Find(uint32_t mip, uint32_t item) const
    {
        if (mip >= m_mipLevels || item >= m_arraySize) return nullptr;
        return &m_subresources[static_cast<size_t>(item) * m_mipLevels + mip];
    }

    /// 늘어놓은 순서 그대로의 서브리소스. DX12 서브리소스 번호와 같은 순서다.
    [[nodiscard]] const Subresource* At(uint32_t index) const
    {
        if (index >= m_subresources.size()) return nullptr;
        return &m_subresources[index];
    }

    [[nodiscard]] const std::byte* PixelsAt(const Subresource& subresource) const
    {
        if (subresource.offset + subresource.slicePitch > m_pixels.size()) return nullptr;
        return m_pixels.data() + subresource.offset;
    }

    [[nodiscard]] std::byte* MutablePixelsAt(const Subresource& subresource)
    {
        if (subresource.offset + subresource.slicePitch > m_pixels.size()) return nullptr;
        return m_pixels.data() + subresource.offset;
    }

private:
    RHIFormat m_format{ RHIFormat::Unknown };
    uint32_t  m_width{ 0 };
    uint32_t  m_height{ 0 };
    uint32_t  m_mipLevels{ 0 };
    uint32_t  m_arraySize{ 0 };
    bool      m_isCube{ false };

    std::vector<std::byte>            m_pixels;
    std::vector<Subresource>          m_subresources;
};
