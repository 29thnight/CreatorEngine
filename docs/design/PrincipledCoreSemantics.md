# Core Principled 의미 — MAT-3

**2026-09-28 · 공용 Slang 평가·직접광·IBL 응답 구현 및 독립 GPU 검증**

## 기준과 소유권

기본값은 MAT-2가 고정한 Blender 5.1.1 [소켓 export](../../Tools/LatticeExample/fixtures/blender-5.1.1-shader-nodes.json)를 따른다.
의미 기준은 같은 버전의 [EEVEE Principled 구현](https://github.com/blender/blender/blob/v5.1.1/source/blender/gpu/shaders/material/gpu_shader_material_principled.glsl)과
[Cycles Principled 구성](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/osl/shaders/node_principled_bsdf.osl)이다.
색과 법선은 이미 scene-linear RGB와 world-space로 변환된 입력이다. texture의 색 공간 변환과
graph→Slang 연결은 MAT-6, cooked resource binding·route·packing은 MAT-7이 소유한다.

## 입력과 평가 결과

| 입력 | 기본값 | 평가 |
|---|---|---|
| Base Color | RGB 0.8 | 음수 제거. 확산의 HDR 값은 보존하고 금속 F0는 0~1 |
| Metallic / Roughness | 0 / 0.5 | 0~1 clamp. 직접광 GGX alpha=roughness², 수치 하한 0.001 |
| IOR | 1.5 | 최소 0.00001. 1이면 비금속 반사 없음. 1 미만에서는 임계각의 전반사 포함 |
| Specular IOR Level | 0.5 | 음수 제거. dielectric F0에 2×level을 적용하고 유효 IOR를 재구성 |
| Specular Tint | RGB 1 | 비금속은 F0 tint, 금속은 F82 Tint. 금속 정면 F0는 Base Color |
| Normal | geometric normal | 미연결 Normal은 호출자가 geometry 문맥을 전달. 0/NaN/overflow는 그 normal로 복구 |
| Alpha | 1 | 0~1 opacity. BSDF/emission 자체를 미리 곱하지 않음. coverage/blend가 별도로 소비 |
| Emission Color / Strength | RGB 1 / 0 | linear RGB×strength. HDR와 연결 값의 부호 보존. 조명·metallic·IOR와 독립 |
| Occlusion | 1 | 엔진 AO 확장. 0~1, 직접광에는 적용하지 않음 |

`DefaultMaterialInputs(geometricNormal)`과 `EvaluateMaterial(inputs, geometricNormal)`이 문맥을 받는다.
geometry가 없는 독립 호출의 기본 normal은 +Z다. 출력의 `ior`는 Specular Level 조절을 반영한
유효 IOR이며 `dielectricF0`, `metalF0`, `metalF82Correction`을 직접광과 IBL이 함께 소비한다.
Blender의 모든 31개 입력 의미가 완료됐다는 뜻은 아니다. Layered는 [MAT-4 계약](PrincipledLayeredSemantics.md), Special은 MAT-5가 소유하며,
EEVEE에서 사용하지 않는 Diffuse Roughness나 Cycles 전용 확장은 별도 지원 판정을 따른다.

## 반사와 조명

- dielectric F0는 `((IOR-1)/(IOR+1))²`에서 시작한다. Specular Level이 기본값과 다르면
  2×level로 조절한 F0에서 유효 IOR를 구하며, 원래 IOR<1인 경우 역수를 유지한다.
  IOR 역변환의 F0는 최대 0.99, tint를 적용한 RGB F0는 최대 1이다.
- dielectric 각도 응답은 Snell 법칙과 편광 Fresnel을 사용해 F0~F90 보간을 구한다.
  IOR=1이나 Specular Level=0은 스침각에서도 비금속 반사를 만들지 않는다.
- 금속은 [F82 Tint 구성](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_microfacet.h)을 따른다.
  Schlick 곡선에 `cos(theta)×(1-cos(theta))⁶` 보정을 적용해 cos=1/7에서 tint를 반영한다.
  Specular Level은 금속 로브를 끄지 않는다. Metallic 중간값은 두 반사 응답을 혼합한다.
- 공용 `BuildPbrSurface`의 직접광은 정적 Specular/IOR bit가 있을 때 이 Fresnel을 사용한다.
  GGX/Smith와 기존 엔진 에너지 보상은 유지한다. EEVEE의 LUT·에너지 감쇠·환경 필터 전체와
  수치 일치하는 판정은 MAT-9에서 한다.

## IBL과 기존 제품 경로

`PrincipledIblIntegral`은 single-scatter RGB, directional albedo, 평균 Fresnel을 갖는다.
`IntegratePrincipledIbl`은 1,024 GGX 표본과 64개 코사인 평균 표본으로 같은 Fresnel을 적분한다.
Roughness=0은 샘플링 오차가 증폭되는 스침각에서도 거울의 해석식으로 평가한다.
`EvaluatePrincipledIbl`은 이 응답과 irradiance/prefiltered/AO를 소비한다. 적분 함수는 bake/probe용이고
현재 제품 pixel shader에서 호출하지 않는다. lookup의 차원·샘플 정책·cache·resource binding은 MAT-6/MAT-7 후속이다.

기존 glTF는 `DefaultStandardMaterialInputs`·`EvaluateStandardMaterial`·`EvaluateStandardIbl`로
white/roughness=1 기본값, raw MRT factor 곱 결과, F0=0.04, Schlick/2채널 DFG를 보존한다.
고정 GBuffer/Forward/Deferred에서 core Specular/IOR bit를 선언하면 `StandardMaterialRoute.slang`이
미배선 진단으로 컴파일을 거부한다. `.shadergraph`의 비기본 IOR가 제품에 연결됐다고 표시하지 않는다.
새 MRT/cbuffer를 MAT-3에서 임의 확장하지 않는다.

MAT-4는 별도의 Layered state와 lobe별 prepared integral을 추가했다. Core/glTF 호출 경계는 유지한다.
공용 지원 mask는 `0x07FF`로 넓어졌고, 고정 pass는 core/Layered의 미배선 요청을 거부한다.
아래 수치는 MAT-3 최초 판정이다. 현재 ABI 거부 34건과 Layered 검증은 위 MAT-4 계약을 따른다.

## 검증

실행 도구는 [verify-principled-core.ps1](../../Tools/regression/verify-principled-core.ps1)과
[verify-material-abi.ps1](../../Tools/regression/verify-material-abi.ps1)이다. VS18 MSVC /W4 /WX,
저장소 Slang 2026.14와 고정 DXC만 사용한다.

| 검사 | 결과 |
|---|---|
| Blender export의 9개 core 소켓 기본값 | shader 기본값 GPU 검사와 별도로 대조 통과 |
| core compute 엔트리 DXIL/SPIR-V | 양 backend 컴파일 통과; SPIR-V 실행은 하지 않음 |
| RTX 4070 Ti 실제 D3D12 compute/readback | 23개 사례×9개 각도, 8,280개 double CPU 기준 대조 통과 |
| core 최대 정규화 오차 | 0.000011920798; 분모=max(1, abs(expected)) |
| 점별 ABI/Fresnel/직접광 허용 오차 | 정규화 0.00005 |
| IBL 표본 적분 허용 오차 | 정규화 0.003; float GGX와 전반사 경계의 quadrature 오차 포함 |
| 고정 pass ABI | DXIL/SPIR-V 70개 엔트리 통과, 미지원/unknown/미배선 mask 10건 지정 진단 거부 |
| 현재 Debug host의 변경 Slang GPU 검증 | GBuffer, Forward+/reference 16,384픽셀 불일치 0, Water/Wind, IBL/AO 통과 |

사례에는 IOR=1/2/<1, Specular=0/1, 양쪽 dielectric tint·금속 F82, metal/dielectric 혼합,
상하 clamp, HDR emission, Alpha=0/0.35, 단위 normal·NaN/overflow·geometric fallback이 포함된다.
raw readback은 `Build/Obj/PrincipledCoreProbe/gpu-readback.csv`, 수치 검사 로그는 같은 폴더의
`gpu.log`, 제품 검사는 `Build/Obj/MaterialAbiProbe/mat3-current-host-results.jsonl`이다.
처음 실행한 옛 배포본은 ForwardCull.hlsl 참조로 실패했고 통과 증거에서 제외했다.
제품 검사는 `Bin/x64-Debug/Editor/CreatorEditor.exe`의 현행 runtime에 변경 Slang을 지정한 결과다.
이번 작업에서 C++ Editor 전체를 새로 빌드한 결과나 Blender EXR 교차 비교로 보고하지 않는다.
대시보드 JavaScript 파싱과 Material 10행·34일/완료 14일 집계는 통과했다. 전체
`verify-plan-dashboard.ps1`은 기존 null-days 행 24건과 PHASE 4.6 산수 1건 때문에 실패하며,
MAT-3 완료 판정을 그 전체 검사 통과로 바꿔 적지 않는다.

MAT-3는 공용 core 의미 구현과 GPU 대조까지 완료다. Editor/LX 연결, graph codegen, 제품의
비기본 IOR lookup/packing, Blender golden·route parity·성능 판정은 LX-3/MAT-6/MAT-7/MAT-9에 남는다.
