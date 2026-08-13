#pragma once
#ifndef DYNAMICCPP_EXPORTS
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
};

#endif
