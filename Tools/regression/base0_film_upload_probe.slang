#define LX_FILM_SENSITIVITY_GPU_TABLE 1
#include "../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang"
RWTexture2D<float4> output : register(u0);
[shader("compute")]
[numthreads(32, 1, 1)]
void VerifyFilmUpload(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= 512u) return;
    output[uint2(i, 0)] = filmSensitivityReal[i];
    output[uint2(i, 1)] = float4(FilmSensitivityReal[i], 0.0);
    output[uint2(i, 2)] = filmSensitivityImag[i];
    output[uint2(i, 3)] = float4(FilmSensitivityImag[i], 0.0);
}
