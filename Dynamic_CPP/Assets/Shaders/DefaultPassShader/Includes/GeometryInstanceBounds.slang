#pragma once

bool GeometryValidInstanceBounds(float3 minimum, float3 maximum)
{
    return all(isfinite(minimum)) && all(isfinite(maximum)) && all(minimum <= maximum);
}

bool GeometryTransformInstanceBounds(float3 minimum, float3 maximum, float4 columns[3], out float3 transformedMinimum,
                                     out float3 transformedMaximum)
{
    transformedMinimum = float3(0.0f);
    transformedMaximum = float3(0.0f);
    if (!GeometryValidInstanceBounds(minimum, maximum))
    {
        return false;
    }
    const float3 magnitude = max(abs(minimum), abs(maximum));
    [unroll] for (uint component = 0u; component < 3u; ++component)
    {
        const float4 column = columns[component];
        const float3 a = minimum * column.xyz;
        const float3 b = maximum * column.xyz;
        const float3 lower = min(a, b);
        const float3 upper = max(a, b);
        // 본 행렬 혼합 후 정점을 곱하는 실제 순서의 취소 오차와 정규화된
        // 네 가중치의 합 오차까지 포함한다. 부호가 다른 축도 바깥으로 넓힌다.
        const float rounding = max(0.00001f, (dot(magnitude, abs(column.xyz)) + abs(column.w)) * 0.00002f);
        transformedMinimum[component] = ((lower.x + lower.y) + lower.z) + column.w - rounding;
        transformedMaximum[component] = ((upper.x + upper.y) + upper.z) + column.w + rounding;
    }
    return GeometryValidInstanceBounds(transformedMinimum, transformedMaximum);
}

bool GeometryInstanceOutsideFrustum(float3 minimum, float3 maximum, float4 projection[4], uint valid)
{
    if (valid == 0u || !GeometryValidInstanceBounds(minimum, maximum))
    {
        return false;
    }
    float4 lowerClip, upperClip;
    const float3 magnitude = max(abs(minimum), abs(maximum));
    [unroll] for (uint component = 0u; component < 4u; ++component)
    {
        const float4 column = projection[component];
        const float3 a = minimum * column.xyz;
        const float3 b = maximum * column.xyz;
        const float3 lower = min(a, b);
        const float3 upper = max(a, b);
        const float rounding = max(0.00001f, (dot(magnitude, abs(column.xyz)) + abs(column.w)) * 0.00002f);
        lowerClip[component] = ((lower.x + lower.y) + lower.z) + column.w - rounding;
        upperClip[component] = ((upper.x + upper.y) + upper.z) + column.w + rounding;
    }
    if (!all(isfinite(lowerClip)) || !all(isfinite(upperClip)))
    {
        return false;
    }
    // 메시렛별 판정의 epsilon보다 넓혀 경계에 걸친 인스턴스를 유지한다.
    const float4 clipMagnitude = max(abs(lowerClip), abs(upperClip));
    const float epsilon =
        max(1.0f, max(max(clipMagnitude.x, clipMagnitude.y), max(clipMagnitude.z, clipMagnitude.w))) * 0.00001f;
    return upperClip.x < -upperClip.w - epsilon || lowerClip.x > upperClip.w + epsilon ||
           upperClip.y < -upperClip.w - epsilon || lowerClip.y > upperClip.w + epsilon || upperClip.z < -epsilon ||
           lowerClip.z > upperClip.w + epsilon;
}

bool GeometryPredictionContainsBounds(float3 minimum, float3 maximum, float4 projection[4])
{
    if (!GeometryValidInstanceBounds(minimum, maximum))
    {
        return false;
    }
    const float4 magnitude = float4(max(abs(minimum), abs(maximum)), 1.0f);
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        const float4 position =
            float4((corner & 1u) != 0u ? maximum.x : minimum.x, (corner & 2u) != 0u ? maximum.y : minimum.y,
                   (corner & 4u) != 0u ? maximum.z : minimum.z, 1.0f);
        float4 clip, rounding;
        [unroll] for (uint component = 0u; component < 4u; ++component)
        {
            clip[component] = dot(position, projection[component]);
            rounding[component] = max(0.00001f, dot(magnitude, abs(projection[component])) * 0.00002f);
        }
        const float minimumW = clip.w - rounding.w;
        // 광원 뷰의 입력은 화면 밖까지 이어질 수 있다. 카메라가 관측하지
        // 않은 영역을 clamp한 깊이로 예측하지 않고 recovery에 그대로 넘긴다.
        if (!all(isfinite(clip)) || !all(isfinite(rounding)) || minimumW <= 0.000001f || clip.z - rounding.z <= 0.0f ||
            clip.z + rounding.z >= minimumW || any(abs(clip.xy) + rounding.xy >= minimumW))
        {
            return false;
        }
    }
    return true;
}
