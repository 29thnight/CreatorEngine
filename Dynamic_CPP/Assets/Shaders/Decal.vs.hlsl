cbuffer PerObject : register(b0)
{
    matrix model;
}

cbuffer PerFrame : register(b1)
{
    matrix view;
}

cbuffer PerApplication : register(b2)
{
    matrix projection;
}

// 정점 입력 구조체
struct VS_INPUT
{
    float3 position : POSITION;
};

// 프래그먼트 셰이더로 전달될 구조체
struct VS_OUTPUT
{
    float4 position : SV_Position; // 클립 공간 좌표
};

// 정점 셰이더 메인 함수
VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output = (VS_OUTPUT) 0;

    // 월드-뷰-프로젝션 변환
    matrix vp = mul(projection, view);
    matrix wvpMatrix = mul(vp, model);

    // 정점 위치를 클립 공간으로 변환
    output.position = mul(wvpMatrix, float4(input.position, 1.0f));

    return output;
}