#pragma once
#include "RHIResourceTypes.h"

#include <string>

class Texture;

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

