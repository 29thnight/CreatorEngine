# MAT-7 공통 Forward+·일반 alpha Blend

**2026-10-02 · 다섯 번째 구현 단계 · MAT-7/PHASE 4.25 진행 중**

## 구현

- `EnhancedForwardPass`가 광원 buffer를 한 번 업로드하고 16×16 tile culling 결과를 `EnhancedForwardLighting`으로 공유한다. Code와 Graph는 같은 count/index, cascade shadow, 읽기 전용 opaque depth와 HDR을 소비한다. tile당 32개 초과는 전체 광원 배열을 평가한다.
- 카메라의 forward 방향에 따른 깊이로 Code/Graph draw를 함께 back-to-front 정렬한다. Code 연속 구간만 기존 batch의 범위를 잘라 기록하고 Graph draw는 그 사이에 기록한다. Graph 재질 lookup capture/bake는 동일 Forward+ 순서 안의 준비다.
- generated ShaderMeta에 실제 `Forward` entry와 Transparent/Alpha state를 추가했다. Core/Layered generation은 shadow·opaque 요청과 함께 alpha color/lookup의 6개 추가 요청까지 총 15개가 Ready일 때 선택한다. graphics owner는 기존 LX 공통 타입을 사용한다.
- Graph Blend는 SRC_ALPHA/INV_SRC_ALPHA이며 depth와 opaque owner mask를 쓰지 않는다. opaque SSAO/Decal을 alpha surface에 적용하지 않고 opaque shadow caster에도 넣지 않는다. Alpha 0은 컬러 출력을 버린다.
- alpha draw는 임시 lookup을 순서대로 재사용한다. opaque temporal cache를 오염시키지 않으며 frame/recording owner와 GPU completion에 따라 자원을 재활용한다. 총 2 GiB payload 예산은 alpha 사용 시 opaque 쌍 2/3, scratch 1/3으로 나눈다.
- Graph texture 범위 t16..127과 충돌하지 않도록 공유 light/count/index를 t128..130에 배치한다. shader ABI/파생 계약 변경으로 **SceneHost identity 11**을 사용하고 기존 10 cook은 재생성한다. LXMC v3·BRDF 1024/environment 4096·기존 품질 상한은 유지한다.

## 검증 방법

`Tools/regression/verify-material-forward-blend.ps1 -SkipDependencyRestore`가 두 구성과 두 native backend를 순서대로 빌드·실행한다. 코드 재질은 실제 `Forward.shadermeta` → LX graphics owner → `SealCore/SealCoverage` → immutable frame snapshot을 사용한다. Graph는 완성된 Scene product를 준비한 뒤 authoring을 비활성화하고 sealed bytecode에서 PSO를 준비한다. 실행 중 Graph compile submission은 0이다.

16×16 fixture 12개: Code/Core Graph/Layered Graph/Code의 isolated 색 4개, 혼합 순차/병렬, 수집 순서 역전, 카메라 forward 역전, opaque 깊이 가림, Alpha 0, 65개 광원의 tiled overflow와 whole-light Reference다. isolated 색에 독립 alpha recurrence를 적용한 4,608개 RGB 성분을 허용 오차 `0.003 + 0.003×abs(expected)`로 대조한다. 65광원 tiled/reference는 `0.001 + 0.002×abs(value)`로 대조하고 모든 프레임의 depth/owner 보존·finite HDR·validation 0을 검사한다. fixture는 시각 FPS나 Blender BRDF 일치도를 측정하지 않는다.

## 실행 결과

| 실제 게이트 | 결과 |
|---|---|
| DX12 Debug mixed GPU | 12 frames·4,608 RGB 대조 성분·21,173 checks·Graph compile 0·validation 0. 최종 특수 transport guard 수정 뒤의 회귀 묶음에서도 재실행해 통과. |
| DX12 Release mixed GPU | 위 12 frames·4,608 성분·21,173 checks·compile/validation 0. 최종 소스 613개 hash 무변경을 포함한 실행 게이트 통과. |
| Vulkan Debug mixed GPU | 12 frames·4,608 성분·21,174 checks·compile/validation 0. 같은 613개 source hash 무변경 통과. Vulkan Release mixed 실행은 이번 단계에서 검증하지 않았다. |
| Debug 전체 에디터 | 최종 엔진·Editor/RenderTests·실행 런처 빌드 통과. `/bigobj`는 빌드 환경에만 적용하고 복원했다. |
| 기존 SSS Debug | 12 frames·119,415 checks·108,072 GPU 성분·validation 0 통과. |
| 기존 refraction Debug | 24 frames·184,696 checks·155,505 GPU 성분·validation 0 통과. |
| 기존 Volume Debug | 33 frames·35,028 checks·57,840 GPU 성분·validation 0 통과. Volume-only에 alpha graphics 요청을 요구하지 않는 조건도 포함한다. |
| 기존 shadow/Decal Debug | shadow 42 frames·33,232 checks·32,256 GPU 성분, Decal 114 frames·295,801 checks·290,898 GPU 성분, 각각 validation 0 통과. |
| Vulkan generation Debug/Release | 각각 768픽셀·Scene program 2개·Ready 요청 30개·PSO worker 23개·stale 거부 65·실패 주입 2·validation 0 통과. 595개 source hash 무변경. polling 검사 수는 실행 시점에 따라 달라진다. |
| 기존 Scene/lookup/generation Debug | 55 frames·세대 교체 11 frames·20,293,812 checks·3,240,423 GPU 성분·stale 거부 1·fallback 6·abort 1 통과. 순차/병렬 합성·texture/LOD·cold/warm lookup·입력 변경·실패 보존 회귀. |
| 대시보드·게이트 구문 | script 1개·42개 phase 진행률 finite, 4.25=80/MAT-7 progress/days:null 유지. 새 PowerShell gate와 갱신한 generation gate 구문 통과. |

로그는 `Build/Obj/MaterialProductProbe/forward-blend-*`, `forward-regression-*-Debug.log`,
`forward-generation-gate.log`에 보존한다. 새 실행 게이트는 `forward-blend-gate-Release.log`와
`forward-blend-vulkan-gate-Debug.log`, 에디터 빌드는 `forward-blend-editor-Debug-build.log`다.
검사 수는 backend별 초기 API 검사 차이를 포함하며 고정 frame/픽셀 성분·합성 오차·validation을 판정값으로 사용한다.

Vulkan 첫 시도는 `WaitSceneProgram`의 90초 준비 제한으로 실패했다. 테스트의 Vulkan 정확성
대기 제한을 600초로 늘린 뒤 같은 합성 조건을 통과했다. 제품 준비 정책·이미지 오차 상한을
변경한 것은 아니다. 재실행 전체 프로세스는 시작/최종 로그 시각 기준 약 326초였고,
compile/PSO/lookup 준비와 렌더를 모두 포함하며 다른 회귀 도구도 함께 실행 중이었다.
이를 순수 PSO 시간이나 모델 FPS 측정으로 쓰지 않는다. native 냉간 준비 비용은
MAT-9 cold/warm 및 PHASE 4.9 backend 성능 게이트에서 분리 측정한다.

Release 링크의 기존 `Utility_Framework.pdb` LNK4020과 에디터 Vulkan delay-load LNK4229
경고는 남는다. 이번 변경으로 해당 디버그 정보·링크 경고를 해결했다고 세지 않는다.

## 후속 상태

보조 compute/RT 조회 정리는 [compute 소유권](MAT7ComputePipelineOwnership.md),
identity 11의 편집·저장·실패 복구와 실제 Player는 [제품 회귀](MAT7ProductEditingRecovery.md),
identity 12의 SSS·굴절·Volume/alpha 혼합은 [공통 transport](MAT7ForwardTransportComposition.md)가 소유한다.
아래 목록은 이 단계 당시의 잔여 이력이며 현행 미지원 목록으로 사용하지 않는다.

<details>
<summary>다섯 번째 단계 당시의 잔여 게이트와 한계</summary>

1. SSS/transmission 또는 Volume을 포함한 재질의 alpha 혼합은 지원하지 않는다. 진단과 함께 같은 슬롯의 마지막 제출된 재질/coverage를 유지하며 cold model은 부분 메시를 게시하지 않는다. 이들 특수 transport와 일반 Blend 혼합, 굴절 배경에 들어가는 투명 객체 순서를 기존 MAT-7 alpha/transmission 게이트에서 확정·검증한다.
2. 정렬 단위는 object/draw다. 교차 표면의 OIT/triangle sorting은 미구현이며 하나의 draw 안의 self-overlap도 screen lookup 한 층을 공유한다. 현재 검증 범위는 분리된 alpha 층의 object 정렬·합성이며 제품 지원 문서에 이 한계를 보존한다.
3. alpha draw마다 lookup capture와 1024/4096 적분이 반복된다. opaque temporal reuse를 alpha에 그대로 적용하지 않는다. 전체 모델·근접 이동 카메라의 CPU/GPU 비용과 route parity는 MAT-9 수용 게이트다. 이번 fixture의 성공을 성능 개선으로 세지 않는다.
4. Preview/Scene/Game의 편집·override·저장 재개방·실패 복구·재import와 identity 11 encrypted cooked Player 재실행은 새 경로의 제품 회귀로 남는다. 이번 Graph fixture는 테스트 준비 단계에서 sealed product를 만들며 실제 AssetCooker/package 실행을 대신하지 않는다.
5. Volume/lookup 보조 compute owner와 native compatibility registry 정리는 기존 MAT-7 통합 잔여다. LX UI 마감은 LX-3, Vulkan 전체 제품 수용은 PHASE 4.9가 소유한다.

MAT-7 `progress`/미산정, PHASE 4.25 열림을 유지한다. 이 변경은 커밋·푸시하지 않았다.

</details>
