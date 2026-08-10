struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 st, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * st.x;
    float cosTheta = sqrt((1.0 - st.y) / (1.0 + (a * a - 1.0) * st.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float GeometrySchlickGGXIBL(float NdotX, float roughness)
{
    float r = roughness;
    float k = (r * r) / 2.0;

    float num = NdotX;
    float denom = NdotX * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmithIBL(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGXIBL(NdotV, roughness);
    float ggx1 = GeometrySchlickGGXIBL(NdotL, roughness);

    return ggx1 * ggx2;
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 st = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(st, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0)
        {
            float G = GeometrySmithIBL(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // DX11 원본은 float2를 반환하지만 RGBA16F 타깃의 나머지 채널이
    // 미정의라는 D3D12 검증 경고가 나온다. 소비자는 RG만 읽으므로
    // BA를 명시해 채운다 — RG 내용은 원본과 같다.
    return float4(IntegrateBRDF(input.texCoord.x, input.texCoord.y), 0.0f, 1.0f);
}
