# MAT-9 박막 확대 대조와 수용 판정 — 2026-10-01

이 문서는 **공통 GGX 수정 전 4/16 결과**를 보존한다. 이후 공통 metal/dielectric
감쇠·GGX 보상·Core fallback 수정으로 on/off 32조건이 통과했다.
현재 판정은 [MAT9GgxClosureComparison](MAT9GgxClosureComparison.md)을 따른다.

## 판정

**현재 전체 박막 rendered 수용은 미달이다.** 기존의 550nm 재질 한 개를 통과선으로
삼지 않고, 두께·IOR·금속성·roughness·coat를 바꾼 8종과 같은 입력의 박막-off 대조군을
추가했다. 방향광/white furnace의 박막 16조건 중 **4조건**만 아래 내부 검증 목표를
통과했다. MAT-9 progress·완료 공수 **32/34일**을 유지한다.

이는 실측에 맞춰 허용치를 늘린 판정이 아니다. 첫 native sweep 전에 선언한
scene-linear 목표는 RGB relative RMS ≤1%, p95 normalized ≤1%, maximum normalized ≤5%,
Blender 독립 시드 간 relative RMS ≤0.25%다. normalized 차이는 각 성분의
`abs(native-reference)/max(1,abs(reference))`다. 이 목표는 지각적 JND나 모든 장면의
시각적 동등성을 보장하는 기준이 아니다. 판정기는 실패 시 exit 2를 반환하고 목표를 변경하지 않는다.

## 고정 조건과 실행 증거

- Blender 5.1.1 `b70da489d7f4`, Cycles CPU 1,024 samples, seed 0/11의 독립 실행.
- 기존 matched contract와 같은 triangle/normal/UV tangent, 카메라, sun, furnace,
  Gaussian 0.01 filter, 한 번의 diffuse/glossy bounce, scene-linear RGBA32F.
- 박막/off 8쌍 × 조명 2종 + emission/white controls = **36장**, 별도 normal/view diagnostics.
  기존 core/area-light/HDRI reference는 보존한다.
- native Release DX12 수정 전/후 각각 **597,673개 검사**, GPU validation 0건, 정상 종료.
  timing 목적 실행이 아니며 CPU/GPU/FPS 수치로 해석하지 않는다.
- 공통 emission mask를 2픽셀 침식한 **345픽셀**을 모든 재질에 동일하게 사용한다.
  normal/view diagnostic 최대 절대 차이 각각 **0.000078693 / 0.000014067**.
- 이미지의 view cosine 범위는 **0.57404~0.99843**다. cosine <0.5 밴드는 빈 밴드로
  기록한다. extreme grazing 이미지 검증으로 세지 않는다.
- film의 독립 시드 변동은 sun 최대 **0.00943%**, furnace 최대 **0.22359%**.
  한 시드 쌍이므로 통계적 신뢰구간이나 보편적인 수렴 상한은 아니다.

원본 입력/triangle/reference 이미지 SHA를 검증하고, native image SHA 및 실제 픽셀에서
판정 지표를 재계산한다. 입력·리포트·픽셀의 불일치는 수용 실패와 별도의 검사 오류다.

고정 seed-0 fixture: `Tools/blender/fixtures/material-thin-film-5.1.1`.
측정 요약: [MAT9ThinFilmAcceptance.json](MAT9ThinFilmAcceptance.json).

## 최종 relative RMS

| 조건 | film sun | off sun | film furnace | off furnace | film 통과 조명 |
|---|---:|---:|---:|---:|---|
| 0.25nm, IOR 1.4, metal 0.6 | 3.0172% | 16.6248% | 1.4294% | 15.6779% | 없음 |
| 100nm, IOR 1.5, dielectric | 0.1697% | 0.5804% | 0.1914% | 0.3042% | 둘 다 |
| 1000nm, IOR 2, dielectric | 1.2285% | 0.5804% | 1.2226% | 0.3042% | 없음 |
| 100nm, IOR 1.5, metal 1 | 5.1095% | 5.5801% | 0.0943% | 2.8933% | furnace |
| 1000nm, IOR 2, metal 1 | 5.0249% | 5.5801% | 0.1017% | 2.8933% | furnace |
| 200nm, IOR 3, metal 0.6 | 3.0386% | 16.6248% | 2.2862% | 15.6779% | 없음 |
| 550nm, IOR 1.4, metal 0.6, rough 0.88 | 6.2026% | 35.5239% | 8.8564% | 19.6764% | 없음 |
| 550nm, IOR 1.4, metal 0.6, coat 0.7 | 2.8348% | 15.5123% | 1.3520% | 15.1905% | 없음 |

off는 모든 박막 입력을 그대로 두고 thickness만 0으로 바꾼 대조군이다. 일부 off의 큰
오차는 박막 연산 이외의 BRDF/레이어 근사가 남았음을 보여준다. off 오차를 film 오차에서
단순히 빼지 않는다. 요약 JSON에 각도 밴드별 film-effect residual도 별도로 기록한다.

## 수정한 실제 차이: subnanometer 전환

[Cycles cutoff](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/svm/types.h)는
0.1nm이며, [Fresnel 구현](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_util.h#L518)은
0.1nm를 넘는 박막에서 1nm까지 **film IOR만** smoothstep으로 완화한다.
엔진은 IOR를 완화한 뒤 최종 dielectric/metal 반사색도 같은 값으로 다시 보간하고 있었다.

- `PrincipledBrdf`에서 이중 결과 보간을 제거하고 cutoff를 >0.1nm로 맞췄다.
- `PrincipledLayered`의 film compensation 활성 조건에도 같은 cutoff를 적용했다.
- CPU complex oracle을 같은 pinned 의미로 수정했다. SceneHost identity **2→3**으로
  이전 쿠킹/컴파일 산출물의 재사용을 막았다.
- 이전 RGB-3 및 spectral numeric golden은 보존한다. cutoff 영향이 있는 두 0.1nm
  fixture의 **112행**만 독립 CPU oracle의 `numeric-golden-transition.csv`로 추가했다.
  0.1nm의 no-film 제어와 **224성분** 일치를 별도로 요구한다.
- Layered DXIL/SPIR-V 14개·거부 8개, GPU/CPU **216,776개**, energy **5,712개** 검사 통과.
  최대 정규화 차이 **1.1920929e-6**. 그 밖의 non-film 역사적 golden도 유지한다.
- Special 28개 DXIL/SPIR-V·거부 22개, GPU/CPU **202,049개**, energy **4,410개** 검사와
  이전 spectral/non-film golden도 통과했다. 최대 정규화 차이는 같은 **1.1920929e-6**이다.

36장 + 3 diagnostics의 수정 전/후 float capture 중 **0.25nm의 sun/furnace 두 장만**
바뀌었다. 나머지 37장은 바이트 동일하다. 0.25nm RMS는 sun 3.0114→3.0172%, furnace
1.4282→1.4294%로 약간 늘었다. 이 수정은 pinned 전환 의미를 고친 것이며 전체 이미지
오차 감소로 주장하지 않는다. 공통 BRDF 보상 오차를 별도로 고쳐야 한다.

## grazing의 별도 광학 한계

1nm 간격 CIE 적분 **2,646 RGB성분**에서 LUT/동일 3차 dense 적분의 최대 절대 차이는
**0.0020715**다. 3차/infinite Airy의 최대 차이는 **0.1246548**로, film IOR 1.5,
100nm, cosine 0.05, substrate n=0.3/k=3/F82=0.7의 red에서 발생했다.

[Cycles도 3차에서 절단한다](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_util.h#L479).
따라서 이 차이를 LUT 보간이나 엔진만의 버그라고 부르지 않는다. 이 광학 측정은 같은
RGB 광학 경계의 차수 절단만 분리하며, 전체 BSDF/image parity나 금속의 물리적 분광
재현을 증명하지 않는다. angle별 최대/RMS와 원시 성분은 `Mat9FilmAcceptance/spectrum.json/.csv`에 보존한다.

## 다음 수정 순서: 같은 MAT-9 정확도 범위

1. **공통 base 금속/유전체 레이어 감쇠:** 현재 엔진의 non-film diffuse는 이미 metallic을
   혼합한 `baseReflectance`의 최대 채널을 사용한 뒤 `(1-metallic)`도 곱한다.
   [Cycles Principled](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/svm/closure.h)는
   별도의 metal/dielectric closure 가중치를 사용한다. 박막-off 혼합 재질의 큰 차이를
   이 조건부터 분리·수정하고, film/non-film 공통 closure 회귀를 같이 판정한다.
2. **GGX multiple scatter 모델:** 엔진의 diffuse 보상항과
   [Cycles의 GGX energy scale](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_microfacet.h#L332)은
   방향 분포가 다르다. pure metal의 sun ~5% 차이는 diffuse 감쇠만으로 설명되지 않는다.
   혼합 closure별 보상과 direct/IBL 소비를 같은 계약으로 정리한 뒤 재대조한다.
3. 동일 고정 16조건과 기존 core/layered controls에서 목표를 통과한 뒤 Special/texture,
   Deferred/Forward 및 실제 Scene 성능 판정으로 진행한다. 새 MAT 공수/완료 행을 추가하지 않는다.

이 코드 차이들은 source 확인이다. 각각의 최종 오차 기여량은 아직 ablation하지 않았으므로
전체 8.8564%를 특정 한 항의 비용/오차로 단정하지 않는다. 이전 64×64 cold/warm 박막
비용 측정은 [기존 보고서](MAT9ThinFilmAndEnvironment.md)를 참고값으로
유지하고 실제 화면 점유율·다중 조명·FPS 수용으로 대체하지 않는다.

## 로컬 evidence

- `Build/Obj/Mat9FilmSweep-reference-{0,11}`, `mat9-film-sweep-reference-{0,11}.log`.
- `Build/Obj/Mat9Images-Release-film-acceptance-{baseline,final}`: build/native log,
  graph 입력·생성 Slang·39 float capture·comparison JSON/PNG.
- `Build/Obj/Mat9FilmAcceptance/{before,final}.json/.log`, `spectrum.json/.csv`.
- `Build/Obj/mat9-film-transition-layered-final.log`, `PrincipledLayeredProbe`.
- `Build/Obj/mat9-film-transition-special.log`, `PrincipledSpecialProbe`.

최초 runner는 기본값과 같은 socket 대입을 실패로 오해하여 중단했다. 실제 저장된
socket 값의 일치로 검사를 고쳤으며, 위 baseline/final은 모두 전체 정상 실행이다.
실제 Editor UI·신규 Player 패키지·현재 Vulkan 전체 Scene·프레임 시간 재검증은 이
이미지/광학 판정의 완료로 세지 않는다.
