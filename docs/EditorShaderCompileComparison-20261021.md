# 빈 캐시 원본 재질 셰이더 컴파일 병목 — 2026-10-21

## 결과와 측정 한계

원본 색상 픽셀 셰이더의 중복 평가 코드를 줄이고 Debug 에디터를 재빌드했다. CreatorRobot과 Aria 각각 빈 엔진 캐시 첫 실행, 해당 캐시 재실행, 별도의 새 빈 엔진 캐시 반복 실행을 실제 콘텐츠 브라우저 → 씬 드래그 앤 드롭으로 확인했다. 기능을 생략하거나 최적화 수준을 낮추지 않았다.

CPU 원본 재질 적용은 CreatorRobot 24.861 → 22.375초(별도 빈 캐시 반복 21.609초), Aria 23.543 → 19.310초(반복 19.408초)로 줄었다. 그러나 새 셰이더의 첫 실행에서는 CPU 이후 GPU 준비가 늘어 CreatorRobot의 전체 원본 제출은 오히려 느려졌다. 이 첫 실행 결과를 제외하지 않는다.

별도 빈 엔진 캐시 반복에서는 원본 제출이 CreatorRobot 22.825초, Aria 20.492초였다. 이 반복에서도 원본 셰이더는 각각 64개·48개 모두 다시 컴파일됐다. 드라이버·OS 캐시는 초기화하지 않았으므로 엔진 캐시와 네이티브 준비 상태를 구분해야 한다. 첫 GPU 준비 증가가 반복에서 사라진 것은 드라이버 재사용의 영향으로 추정하지만 드라이버 내부 cache hit를 직접 계측한 결론은 아니다.

## 병목 근거

직전 실행의 shader trace를 entry별로 집계했다. 가장 큰 비용은 소스 분석이나 컴파일 슬롯 대기가 아니라 `getEntryPointCode`의 코드 생성·최적화 구간이었다. 이 시간은 Slang과 그 하위 DXIL 생성 전체를 포함하며, Slang 내부 최적화와 DXC 내부 시간을 별도로 나눈 계측은 아니다.

| 변경 전 원본 작업 시간 합계 | CreatorRobot / 4개 재질 | Aria / 3개 재질 |
|---|---:|---:|
| 원본 Verify 요청 전체 | 88.664초 | 64.351초 |
| ColorPS 코드 생성 | 30.867초 | 25.294초 |
| CachedTemporalPS 코드 생성 | 10.679초 | 7.853초 |
| TemporalPS 코드 생성 | 10.016초 | 6.963초 |
| CachedGBufferPS 소스 분석 | 1.227초 | 0.837초 |
| 전체 요청 슬롯 대기 | 0.779초 | 0.374초 |

재질별 작업은 병렬이다. 위 합계를 사용자가 기다리는 벽시계 시간으로 해석하면 안 된다. 기존 ModuleReuseScope로 대부분의 entry 소스 분석은 이미 재사용되고 있었다. 슬롯 수를 늘리거나 캐시 검증을 생략하는 변경은 하지 않았다.

## 구현과 독립 대조

`Includes/MaterialGraphSceneHost.slang`의 `LXSceneColorPS`는 일반 래스터 경로의 조기 반환과 재구성 루프 안에서 큰 `LXSceneColorPSEvaluate` 본문을 각각 호출했다. 일반 경로는 한 번, 재구성은 기존의 고유 quad identity마다 평가하도록 제어 흐름을 합쳐 평가 호출 지점을 하나로 만들었다. 재구성의 helper lane, 중복 identity 필터, owner discard, front face, 출력 선택을 유지한다.

O3·precise math, 재질·조명·그림자·alpha 기능, lookup·temporal entry 및 제품 완성 계약을 유지했다. SceneHostIdentity를 41 → 42로 올려 기존 cooked 제품과 새 host를 구분하고 대체 재질 패키지도 다시 생성했다.

동일한 실제 Robot 생성 소스와 컴파일 옵션으로 `MaterialCompileTimingProbe`를 실행했다. 두 대조 모두 별도 빈 캐시를 썼다.

| 동일 재질 entry 대조 | 변경 전 | Color 공유 평가 적용 |
|---|---:|---:|
| ColorPS 코드 생성 | 5.228초 | 3.246초 |
| ColorPS Verify 전체 | 5.822초 | 3.816초 |
| ColorPS DXIL 크기 | 327,316 bytes | 223,592 bytes |
| TemporalPS 크기 | 84,660 bytes | 84,660 bytes |
| CachedTemporalPS 크기 | 87,352 bytes | 87,352 bytes |
| GBufferPS 크기 | 88,592 bytes | 88,592 bytes |

GBuffer에도 같은 변경을 시험했으나 코드 생성이 약 1.053 → 1.051초로 유의미하게 줄지 않아 GBuffer·lookup 확장은 되돌렸다. 최종 제품 변경은 ColorPS와 host identity뿐이다. 바이트코드 크기 감소가 프레임 GPU 성능 향상의 증거는 아니며, 이번에는 프레임 성능 개선을 주장하지 않는다.

실제 UI 첫 빈 캐시 실행의 ColorPS 코드 생성 합계도 Robot 30.867 → 20.099초, Aria 25.294 → 13.506초로 줄었다. 전체 원본 요청 합계는 Robot 88.664 → 76.554초, Aria 64.351 → 51.986초였다.

## 실제 에디터 드롭 비교

단위: 드롭 요청 이후 초. GPU 값은 해당 재질 draw의 렌더 제출 시점이며 Present/최초 픽셀 시각과 같지 않다. 변경 전은 직전 `EditorRenderWarmupComparison-20261021.md` 실행 결과다. 변경 후 각 기본 조건 1회와 모델별 별도 빈 캐시 반복 1회를 모두 기록했다.

| 조건 | 대체 제출 전 → 후 | CPU 적용 전 → 후 | 원본 제출 전 → 후 | 변경 후 CPU → 원본 |
|---|---:|---:|---:|---:|
| Robot 첫 빈 캐시 | 2.043 → 2.574 | 24.861 → 22.375 | 26.109 → **27.573** | 5.198 |
| Robot 재실행 | 1.992 → 1.992 | 2.356 → 2.277 | 3.594 → 3.443 | 1.166 |
| Aria 첫 빈 캐시 | 1.175 → 1.225 | 23.543 → 19.310 | 24.409 → 23.711 | 4.401 |
| Aria 재실행 | 1.175 → 1.204 | 1.354 → 1.421 | 2.543 → 2.526 | 1.105 |

| 추가 검사: 별도의 빈 엔진 캐시 | 대체 제출 | CPU 적용 | 원본 제출 | CPU → 원본 |
|---|---:|---:|---:|---:|
| Robot | 2.009 | 21.609 | 22.825 | 1.216 |
| Aria | 1.192 | 19.408 | 20.492 | 1.084 |

첫 Robot은 indexed-ready가 CPU 적용 후 약 4.36–4.97초였으며, 네이티브 mesh 생성은 최장 수십 ms였다. Aria의 마지막 indexed-ready도 약 4.12초였다. 반면 별도 빈 엔진 캐시 반복의 CPU 이후 전체 대기는 약 1.1–1.2초였다. 셰이더 컴파일 비용 감소와 첫 indexed 네이티브 준비의 변동을 구분한다. 첫 실행 전체 개선을 보장하지 않으며, 아직 남은 첫 네이티브 준비 비용을 해결했다고 표시하지 않는다.

네 기본 실행 및 두 반복 모두 드롭 중 공통 셰이더 컴파일 0회, 공통 요청 메모리 적중 48회, 대체 재질 컴파일 0회였다. 빈 캐시에서는 원본 Robot 64개·Aria 48개가 컴파일되고 재실행 원본 컴파일은 0개였다. 시작 로딩 공통 준비는 첫 기본 실행 약 10.85–11.00초, 재실행 약 0.47–0.48초였다.

## 화면과 입력 관측

모델 배치는 모두 네이티브 UI 드래그다. commandlet 모델 배치나 자동 생성 씬을 성능 수치로 사용하지 않았다. 네 기본 조건에서 원본 화면과 선택·기즈모 이동 후 Inspector Z `0.219 → 0.839`를 확인했다.

| 실행 | 화면·입력 증거 |
|---|---|
| Robot 첫 빈 캐시 | 2.772초 캡처 미표시, 9.533초 체커. 20.401초 체커 선택 응답. 30.539초 원본·이동 반영 |
| Robot 재실행 | 3.221초 체커 캡처. 13.512초 선택 시 원본. 24.893초 이동 반영 |
| Aria 첫 빈 캐시 | 2.997초 체커. 14.497초 체커 선택 응답. 25.403초 원본·이동 반영 |
| Aria 재실행 | 3.217초 원본. 123.534초 선택, 221.434초 이동 반영 |
| 별도 빈 캐시 Robot / Aria | 3.089 / 2.948초 체커, 38.421 / 38.032초 관측에는 원본 |

관측 사이에는 도구·분석 공백이 있다. 특히 Aria 재실행의 늦은 선택·이동 시각은 로딩 대기 시간이 아니다. 이 표는 실제 관측 상한이며 최초 조작 가능 시각의 정밀 계측으로 사용하지 않는다. GPU 제출 로그와 실제 화면 관측을 혼용하지 않는다. 첫 빈 캐시 두 모델 모두 선택 시 체커였지만 이동 관측 시에는 원본이었으므로 체커 상태 이동까지 이번 실행에서 확인했다고 확대하지 않는다.

## 빌드·검증·보존

- Debug 에디터 빌드 성공: 오류 0, 경고 14(ProfilerViewer 등의 재정의·지역 변수 관련). 초기 타이밍 probe 빌드는 SolutionDir 경로 형태 문제로 실패했고, 일반 빌드 경로로 다시 빌드해 성공했다. 최종 runtime SHA256 `CF64E63C30882A645EC468DDA2FB6A279A9529735B6FF7E21889CD8E225876F8`.
- `MaterialCompileTimingProbe` 변경 전/Color 변경 후 컴파일·reflection 통과. 이 probe는 GPU draw 검사가 아니므로 렌더 동작은 다음 게이트로 별도 확인했다.
- 재빌드한 `MaterialRasterSurfaceProbe --visibility-reconstruction-only` 실제 DX12 readback 통과: `METEOR_VISIBILITY_RECONSTRUCTION_OK frames=32 partitions=3 footprintFrames=8 validation=0`, `LX_MATERIAL_BUCKET_OK frames=32 validation=0`. 기존 probe 실행 파일은 새 옵션을 지원하지 않아 처음 거부됐으며, 재빌드 후 통과했다.
- 대체 패키지 재생성 성공. 네 기본 실행과 두 반복 모두 패키지 적중 11단계, 프로세스 종료 코드 0. 앞선 회차의 Slang 종료 오류를 이번 수정으로 해결했다는 주장은 하지 않는다.
- 직접 Debug 실행 파일, 디버거 미부착, DX12 Debug Layer 사용, GPU validation 미사용. `--console`은 결과 수집·종료용이다. Release·Vulkan 실시간·전체 배포는 검증하지 않았다.
- 기존 사용자 셰이더 캐시 263,128개 파일의 이름·크기·수정 시각 보존, 추가·누락 0. 모든 테스트에 별도 엔진 캐시를 썼고 드라이버/OS/import 캐시는 초기화하지 않았다.
- 모델 원본 hash 보존: Robot `58D779AFDE1A7FBD13332999C8B11890FB7E10FF2036CA6AE3645D14BEDBA402`, Aria `8ED22A7E3CF7E4F7087A45E83EEBF327B283F0162622F0051CF8AE45833FB0E7`.
- active.workspace 시작 바이트 복원: `D748399BF2184E3A20357E8730A4E7D7F5C26F3BA491ABD1E52E5F4375F791DC`. 관련 diff 공백 검사 통과. 기존 다른 변경은 유지했으며 커밋·푸시는 하지 않았다.

원시 자료는 `Build/editor-shader-compile-20261021` 아래 build/probe 로그, `timing-*`, `after-*`, `repeat-*`, UI 캡처, execution.json, ui-events.json, summary.json과 comparison.json에 있다. `profile.py`는 이전 entry 병목을, `summarize.py`는 드롭 직전 로그 offset부터 이번 실행을 집계한다.
