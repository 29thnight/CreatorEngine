#pragma once

// 접선 프레임 재구성. 노멀맵을 쓰는 패스가 전부 이 한 곳을 부른다 —
// 패스마다 직교화 규칙이 갈리면 같은 재질이 불투명과 투명에서 다르게 보인다.

struct TangentFrame
{
    float3x3 basis;      // 행 = (T, B, N)
    bool     degenerate; // 쓸 수 없는 탄젠트가 들어왔다

    float3 Rotate(float3 tangentSpaceNormal)
    {
        return normalize(mul(tangentSpaceNormal, basis));
    }
};

// normal    : 보간된 기하 법선(정규화 전이어도 된다)
// tangent   : 보간된 탄젠트
// bitangent : 저장된 종법선. 핸디드니스(UV 미러)만 여기서 가져온다.
//
// ★ 퇴화 판정이 있는 이유 ─ 소스에 TANGENT 속성이 없는 자산이 흔하다
//   (Gunner_F_Mythic.glb 도 POSITION/NORMAL/TEXCOORD_0/JOINTS_0/WEIGHTS_0
//   뿐이다). 그런 자산은 임포터의 mikktspace 생성 패스에 전적으로 의존하고,
//   그 패스가 전제를 못 채워 건너뛰면 탄젠트가 0 으로 남는다.
//
//   0 벡터에 그람-슈미트를 걸면 normalize(0) = NaN 이고, NaN 은 이후 모든
//   연산을 통과해 화면에 남는다. 원인이 "탄젠트가 없다"로 보이지 않는다.
//   여기서 걸러 기하 법선으로 물러나면, 증상이 **노멀맵이 안 먹은 매끈한
//   표면**이 되어 원인과 증상이 같은 말을 한다.
TangentFrame BuildTangentFrame(float3 normal, float3 tangent, float3 bitangent)
{
    TangentFrame frame;

    const float3 n = normalize(normal);

    // 그람-슈미트 — 보간을 거치면 T 와 N 이 어긋나고, 그대로 쓰면 조명이
    // 미묘하게 틀어진다.
    const float3 projected = tangent - n * dot(n, tangent);
    const float  lengthSq  = dot(projected, projected);

    // 1e-8 은 float 에서 normalize 가 의미를 잃는 지점이다. 탄젠트가 0
    // 이거나 법선과 평행하면(둘 다 실제로 일어난다) 여기 걸린다.
    frame.degenerate = !(lengthSq > 1e-8);
    if (frame.degenerate)
    {
        frame.basis = float3x3(float3(1.0, 0.0, 0.0),
                               float3(0.0, 1.0, 0.0),
                               n);
        return frame;
    }

    const float3 t = projected * rsqrt(lengthSq);

    // 종법선은 방향(핸디드니스)만 원본에서 가져온다. cross 로 다시 만들기만
    // 하면 UV 가 뒤집힌 메시에서 조명이 반대로 나온다.
    const float3 crossed    = cross(n, t);
    const float  handedness = (dot(crossed, bitangent) < 0.0) ? -1.0 : 1.0;
    const float3 b          = crossed * handedness;

    frame.basis = float3x3(t, b, n);
    return frame;
}

// 접선 공간 노멀맵 표본을 월드 법선으로. 퇴화 프레임이면 기하 법선을
// 그대로 돌려준다 — 축이 |N.z| ~ 1 근처에서 뒤집히는 임의 프레임으로
// 노멀맵을 돌리면 표면에 이음매가 생긴다.
float3 ApplyTangentSpaceNormal(TangentFrame frame, float3 geometricNormal,
    float3 sampledNormal, float normalScale)
{
    if (frame.degenerate) return normalize(geometricNormal);

    float3 tangentSpace = sampledNormal;
    tangentSpace.xy *= normalScale;
    return frame.Rotate(tangentSpace);
}
