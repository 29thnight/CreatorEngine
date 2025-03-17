cbuffer UniformBuffer : register(b0)
{
    float4 gridColor;
    float4 checkerColor;
    float fadeStart;
    float fadeEnd;
    float unitSize;
    int subdivisions;
    float majorLineThickness;
    float minorLineThickness;
    float minorLineAlpha;
    float3 centerOffset;
};
// ��� ����ü: Ŭ�� ��ǥ�� �Բ� ���� ��ǥ�� TEXCOORD0�� �����մϴ�.
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

float grid(float2 pos, float unit, float thickness)
{
    // fwidth�� HLSL������ �����ϰ� ��� ����
    float2 threshold = fwidth(pos) * thickness * 0.5 / unit;
    float2 posWrapped = pos / unit;
    // HLSL������ step�� �����Ƿ� ���� �����ڷ� ����
    float2 line = (frac(-posWrapped) < threshold) ? float2(1.0, 1.0) : float2(0.0, 0.0);
    line += (frac(posWrapped) < threshold) ? float2(1.0, 1.0) : float2(0.0, 0.0);
    return max(line.x, line.y);
}

float checker(float2 pos, float unit)
{
    float square1 = (frac(pos.x / unit * 0.5) >= 0.5) ? 1.0 : 0.0;
    float square2 = (frac(pos.y / unit * 0.5) >= 0.5) ? 1.0 : 0.0;
    return max(square1, square2) - square1 * square2;
}

// Pixel Shader: XZ ������ worldPos.xy ��� worldPos.xz�� ����Ͽ� grid�� ����մϴ�.
float4 main(VS_OUTPUT input) : SV_TARGET
{
// ����: �̹� ���ؽ� ���̴����� ���� ��ǥ�� ����� input.WorldPos�� �־��ٰ� �����մϴ�.
    float3 posWorld = input.worldPos;
    
    // ������ �Ÿ� ��� (XZ ���)
    float distPlanar = distance(posWorld.xz, centerOffset.xz);
    
    // ������ ���ΰ� ���� �����(���̳�) ���� ���
    float line = grid(posWorld.xz, unitSize, majorLineThickness);
    line += grid(posWorld.xz, unitSize / subdivisions, minorLineThickness) * minorLineAlpha;
    line = saturate(line); // clamp(0.0,1.0)�� ����
    
    // üĿ���� ���� ���
    float chec = checker(posWorld.xz, unitSize);
    
    // �Ÿ� ���̵� ���
    float fadeFactor = 1.0 - saturate((distPlanar - fadeStart) / (fadeEnd - fadeStart));
    
    // ���� ���İ� (������ ���Ͽ� ���� ���� �ջ� �� ���̵� ����)
    float alphaGrid = line * gridColor.a;
    float alphaChec = chec * checkerColor.a;
    float alpha = saturate(alphaGrid + alphaChec) * fadeFactor;
    
    // ���� ���� (������Ƽ�ö��̵� ���� ����)
    float3 color = (checkerColor.rgb * alphaChec) * (1.0 - alphaGrid) + (gridColor.rgb * alphaGrid);
    
    return float4(color, alpha);
}