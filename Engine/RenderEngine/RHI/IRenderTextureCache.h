#pragma once
#include "RHIResourceTypes.h"

#include <array>
#include <cstdint>
#include <string>

class Texture;

/// W3 — 저작으로 없는 텍스처 슬롯을 대신하는 **중립 픽셀의 정본**.
///
/// ★ 값이 두 벌이면 언젠가 갈린다. 실제로 한 번 갈렸다 — ORM 의 B 는 셰이더가
///   금속을 `orm.b + metallic` 으로 **더하던** 시절에 0 이 중립이었고, `a2e5ecdc`
///   가 결합을 곱셈으로 바꾸면서 전제가 뒤집혔는데 상수가 따라가지 않았다. 그
///   뒤로 ORM 텍스처가 없는 재질은 저작한 metallic 과 무관하게 전부 비금속으로
///   그려졌다. 한쪽 백엔드만 고치면 같은 일이 백엔드 사이에서 반복된다.
///
/// ★ 계획 §4 의 W3 조건은 "neutral 의 **논리 값**이 backend 와 무관하다" 이다.
///   그것을 지키는 방법은 두 구현이 같은 숫자를 적는 것이 아니라 **숫자가 하나만
///   존재하는 것**이다. 백엔드 구현은 여기서 읽어 간다.
namespace RHINeutralTexel
{
    /// base color · normal · emissive(현행) · AO 미저작, 그리고 업로드 실패 대체.
    inline constexpr std::array<std::uint8_t, 4> kWhite{ 255, 255, 255, 255 };

    /// legacy 세대의 emissive 미저작. 더하는 슬롯이라 중립이 0 이다.
    inline constexpr std::array<std::uint8_t, 4> kBlack{ 0, 0, 0, 255 };

    /// R 오클루전 · G 거칠기 · B 금속 — 셋 다 저작 factor 가 곱해지는 슬롯이므로
    /// 중립은 1 이다. glTF 규격도 "MR 텍스처가 없으면 metallic = metallicFactor" 다.
    inline constexpr std::array<std::uint8_t, 4> kOrmNeutral{ 255, 255, 255, 255 };
}

/// CPU 자산 텍스처를 현재 백엔드의 GPU 리소스로 올리는 캐시.
/// 반환 값과 수명 규약은 백엔드 중립이고, 캐시가 리소스를 소유한다.
class IRenderTextureCache
{
public:
    virtual ~IRenderTextureCache() = default;

    virtual RHITextureEntry GetOrUpload(Texture* texture, std::string& outError) = 0;
    virtual RHITextureEntry GetBlackTexture(std::string& outError) = 0;
    virtual RHITextureEntry GetOrmNeutralTexture(std::string& outError) = 0;

    /// W9 — 업로드 실패로 중립 텍스처를 대신 내준 횟수.
    ///
    /// ★ 이 캐시는 CPU 픽셀이 없거나 업로드가 실패하면 **흰색**을 돌려준다.
    ///   그 자체는 옳다(그리기는 계속되어야 한다). 문제는 그것이 조용하다는
    ///   것이다 — 재질이 흰색으로 보이는 프레임과 정말 흰 재질을 밖에서
    ///   구분할 수 없었다. 계획의 "silent neutral substitution 0"이 이 축이다.
    ///   저작으로 없는 슬롯(AO 미저작 등)의 중립값은 여기 세지 않는다.
    virtual uint32_t GetUploadFailureCount() const = 0;
};

