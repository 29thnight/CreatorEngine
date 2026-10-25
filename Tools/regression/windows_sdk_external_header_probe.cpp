import std;
import "Creator.WinAPI.h";
import "Creator.GraphicsSDK.h";

inline constexpr int ImportedMathVersion = DIRECTX_MATH_VERSION;
#include <DirectXTex.h>

static_assert(ImportedMathVersion == DIRECTX_MATH_VERSION);

bool VerifyExternalSdkHeaders()
{
    const DirectX::XMFLOAT3 value(1.0f, 2.0f, 3.0f);
    DirectX::ScratchImage image;
    return value.y == 2.0f && image.GetImageCount() == 0;
}
