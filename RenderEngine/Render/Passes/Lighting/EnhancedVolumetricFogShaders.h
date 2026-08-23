#pragma once

// 볼류메트릭 포그 셰이더 셋 (PHASE 3-6, 미구현 패스 이식 4차).
//
// DX11 VolumetricFog.cs.hlsl · VolumetricFogAccumulation.cs.hlsl ·
// VolumetricFogComposite.ps.hlsl · VolumetricFog.hlsli의 이식이다.
// 수식과 상수는 그대로 옮겼다 — 그림의 기준선이다.
//
// 바꾼 것은 셋뿐이다:
//
//   · 행렬 규약을 DX12 쪽으로 통일했다. 원본은 한 셰이더 안에서도
//     mul(v, M)(월드 복원)과 mul(M, v)(그림자·구름)를 섞어 쓰고 C++이
//     행렬마다 전치 여부를 달리 맞춰 줬다. 여기서는 전부 CPU가 전치하고
//     셰이더가 mul(v, M)로 통일한다 — 수학적으로 같다.
//   · 정점을 SV_VertexID 풀스크린 삼각형으로(합성).
//   · 누적 셰이더에서 읽지 않는 t0·t1 선언을 뺀다.
//
// ★ 옮기지 않은 것은 없다. 태양 항의 주석 처리, 구름의 isOn 무시,
//   누적의 경계 검사 부재는 원본의 결함이지만 그대로 둔다.

// 프록셀 격자와 월드 좌표를 오가는 헬퍼(VolumetricFog.hlsli).
//
// z는 지수 분포다 — near 근처를 촘촘하게 쪼개려는 것이고, 그래서
// 슬라이스 번호와 뷰 깊이 사이가 로그 관계다.

// ── ① 산란 ──
//
// 프록셀마다 광원 목록을 돌며 위상 함수를 먹이고, 그림자맵과 구름으로
// 가려짐을 곱한 뒤 지난 프레임 격자와 섞는다.
constexpr const char* kFogScatterFile = "FogScatter.hlsl";

// ── ② 누적 ──
//
// z를 따라 앞에서 뒤로 훑으며 투과율을 곱해 나간다. 슬라이스마다
// 상수 투과율을 쓰지 않고 깊이에 대한 적분을 쓰는 것이 Frostbite의 개선이다.
//
// ★ 원본 그대로 경계 검사가 없다. 디스패치가 96행이라 y가 90~95인 스레드가
//   격자 밖에 쓰는데, D3D가 범위 밖 UAV 쓰기를 버려 사고는 나지 않는다.
constexpr const char* kFogAccumulateFile = "FogAccumulate.hlsl";

// ── ③ 합성 ──
//
// 화면 픽셀의 월드 좌표로 격자를 샘플해 씬 색에 얹는다.
// inputColor * 투과율 + 쌓인 산란광이 곧 이 픽셀이 보는 것이다.
constexpr const char* kFogCompositeFile = "FogComposite.hlsl";

