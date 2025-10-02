#define USE_DIFFUSE 1 << 0
#define USE_NORMAL  1 << 1
#define USE_ORM     1 << 2

cbuffer PS_CONSTANT_BUFFER : register(b0)
{
    matrix g_inverseViewMatrix; // 카메라 View-Projection의 역행렬
    matrix g_inverseProjectionMatrix; // 카메라 View-Projection의 역행렬
    float2 g_screenDimensions; // 화면 해상도 (너비, 높이)
};

cbuffer PS_DECAL_BUFFER : register(b1)
{
    matrix g_inverseDecalWorldMatrix; // 데칼 경계 상자 World의 역행렬
    uint g_useFlags;
    uint sliceX;
    uint sliceY;
    int sliceNum;
};

struct Output
{
    float4 outDiffuse : SV_TARGET0;
    float4 outNormal : SV_TARGET1;
    float4 outORM : SV_TARGET2;
};

// G-Buffer 텍스처들
Texture2D g_depthTexture : register(t0);
Texture2D g_albedoTexture : register(t1);
Texture2D g_normalTexture : register(t2);
Texture2D g_ormTexture : register(t3);

// 데칼 텍스처
Texture2D g_decalTexture : register(t4);
Texture2D g_decalNormalTexture : register(t5);
Texture2D g_decalORMTexture : register(t6);

// 샘플러
SamplerState g_linearSampler : register(s0); // 데칼 텍스처 샘플링용
SamplerState g_pointSampler : register(s1); // G-Buffer 샘플링용

// 정점 셰이더에서 넘어오는 입력
struct PS_INPUT
{
    float4 position : SV_Position;
};

// 최종 출력 색상
Output main(PS_INPUT input) : SV_Target
{
    // 1. 화면 UV 좌표 계산
    float2 screenUV = input.position.xy / g_screenDimensions;
    
    // 2. G-Buffer에서 깊이 값 샘플링
    float depth = g_depthTexture.Sample(g_pointSampler, screenUV).r;

    // 깊이 값이 1.0이면 하늘(배경)이므로 데칼을 그리지 않음
    if (depth >= 1.0f)
    {
        discard;
    }
    
    // 3. 월드 좌표 복원
    // 화면 UV와 깊이 값으로 클립 공간 좌표를 생성
    float2 clipUV = screenUV * float2(2.0, -2.0) - float2(1.0, -1.0);
    float4 clipPos = float4(clipUV, depth, 1.0);
    
    // 카메라의 View-Projection 역행렬을 곱해 월드 좌표를 계산
    float4 viewSpace = mul(g_inverseProjectionMatrix, clipPos);

    // perspective divide
    viewSpace /= viewSpace.w;

    float4 worldPos = mul(g_inverseViewMatrix, viewSpace);
    
    // 4. 월드 좌표를 데칼의 로컬 공간으로 변환
    float3 decalLocalPos = mul(g_inverseDecalWorldMatrix, float4(worldPos.xyz, 1)).xyz;
    //decalLocalPos /= decalLocalPos.w;
    
    // 5. 데칼 경계 상자(Unit Cube: -0.5 ~ +0.5)를 벗어나는 픽셀은 폐기
    if (abs(decalLocalPos.x) > 0.5 || abs(decalLocalPos.y) > 0.5 || abs(decalLocalPos.z) > 0.5)
    {
        discard;
    }
    
    // 6. G-Buffer에서 노멀과 알베도(기본 색상) 샘플링
    // 노멀은 월드 공간에 저장되어 있다고 가정
    float3 worldNormal = g_normalTexture.Sample(g_pointSampler, screenUV).xyz * 2 - 1;
    float4 baseAlbedo = g_albedoTexture.Sample(g_pointSampler, screenUV);
    float4 baseORM = g_ormTexture.Sample(g_pointSampler, screenUV);

    // 7. (선택 사항) 노멀을 이용한 폐기
    // 데칼의 앞 방향 (로컬 Z축)과 표면의 노멀을 비교
    // 데칼이 표면의 뒷면에 투영되는 것을 방지
    float3 decalForward = normalize(mul((float3x3) g_inverseDecalWorldMatrix, float3(0, 1, 0)));
    //if (dot(worldNormal, decalForward) < 0.1f) // 0 대신 작은 임계값을 주어 가장자리 아티팩트 방지
    //{
    //    discard;
    //}
    
    float3 up = abs(worldNormal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, worldNormal));
    float3 bitangent = normalize(cross(worldNormal, tangent));
    float3x3 TBN = float3x3(tangent, bitangent, worldNormal);
    
    // 8. 데칼 텍스처 샘플링
    // 데칼 로컬 좌표(-0.5 ~ 0.5)를 텍스처 UV(0 ~ 1)로 변환
    float2 decalUV = decalLocalPos.xz + 0.5;
    decalUV.y = 1-decalUV.y;
    //return float4(decalUV, 0, 1);
    float2 sliceUV = 1.0 / float2(sliceX, sliceY);
    float2 sliceIndex = float2(sliceNum % sliceX, sliceNum / sliceX);
    decalUV = (decalUV + sliceIndex) * sliceUV;
    
    float4 decalSample = g_decalTexture.Sample(g_linearSampler, decalUV);
    
    decalSample.rgb = pow(decalSample.rgb, 2.2);
    
    float3 nTex = g_decalNormalTexture.Sample(g_linearSampler, decalUV).xyz * 2 - 1;
    float3 nWS = normalize(mul(nTex, TBN));
    
    float3 decalorm = g_decalORMTexture.Sample(g_linearSampler, decalUV).xyz;
    float3 orm = float3(decalorm.b, decalorm.g, decalorm.r);
    
    // 9. 색상 혼합 (Blending)
    // 틴트 적용
    //decalSample.rgb *= g_decalColorTint;
    
    // 데칼 텍스처의 알파 값을 이용해 기본 알베도 색상과 혼합
    float3 finalColor = lerp(baseAlbedo.rgb, decalSample.rgb, decalSample.a);
    Output output;
    output.outDiffuse = (g_useFlags & USE_DIFFUSE) != 0 ? float4(finalColor, decalSample.a) : float4(0, 0, 0, 0);
    output.outNormal = (g_useFlags & USE_NORMAL) != 0 ? float4(nWS * 0.5 + 0.5, 0) : float4(0, 1, 0, 0);
    output.outORM = (g_useFlags & USE_ORM) != 0 ? float4(orm, baseORM.a) : float4(0, 0, 1, 0);
    
    return output;
    

    // 최종 색상 출력
    //return float4(finalColor, 1.0f);
}