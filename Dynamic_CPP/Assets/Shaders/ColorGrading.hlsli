float3 ApplyExposure(float3 color, float exp)
{
    return color * exp;
}

float3 ApplyContrast(float3 color, float cont)
{
    // 0.5�� �������� ��� ����
    return (color - 0.5f) * cont + 0.5f;
}

float3 ApplySaturation(float3 color, float sat)
{
    // NTSC ǥ�� �ֵ�(Luminance) ���
    float lum = dot(color, float3(0.299, 0.587, 0.114));
    return lerp(lum.xxx, color, sat);
}

// --- ȭ��Ʈ �뷱�� �Լ� ---

float3 ApplyWhiteBalance(float3 color, float temp, float tnt)
{
    // �� �µ��� R�� B ä�ο� ������ ��
    color.r += temp * 0.1f; // 0.1�� �ΰ��� ������
    color.b -= temp * 0.1f;

    // ������ G ä�ο� ������ ��
    color.g += tnt * 0.1f;

    return color;
}