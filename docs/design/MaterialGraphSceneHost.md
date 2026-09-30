# Material Graph 실제 Scene GBuffer·공유 깊이·컬러 합성

## 1. 설치 범위

`EnhancedSceneRenderer`의 DX12/Vulkan live pipeline에 `LX.Scene.GBuffer`와
`LX.Scene.Color`를 연결했다. 입력은 [SceneViewInput](MaterialGraphSceneInput.md)이
소유한 geometry·world·현재 pose·camera·불변 material instance다.

2026-09-29 [Scene lookup 캐시](MaterialGraphSceneLookup.md)를 추가했다. 현재 host는
4,096²픽셀·4,096 draw와 2 GiB lookup payload 예산을 사용한다. 정확한 평가 입력을
compute에서 비교하고 변경 픽셀의 IBL만 적분한다. 컬러 fragment는 lookup을 소비한다.
카메라 이동 시 전체 갱신 비용과 입력 MRT 대역폭·자원 할당 비용의 실시간 수용은 별도다.
기존 evaluated-point baker의 `PrincipledEnvironmentBake.slang` 수식과 정확도를 유지한다.

지원은 Core/Layered 및 [SSS surface](MaterialGraphSceneSubsurface.md),
[단일 표면 transmission/refraction](MaterialGraphSceneRefraction.md)의 Opaque/Masked와
double-sided coverage다.
[이미지별 Vector·LOD](MaterialGraphTextureFootprints.md)로 둘 이상의 텍스처도 처리한다.
[균질 Volume](MaterialGraphSceneVolume.md)도 별도 transport 경로로 연결했다.
Blended coverage와 cooked Scene host가 없는 packaged 실행은 진단과 함께 거부한다.
이전 preview shader를 Scene shader로 이름만 바꾸어 사용하지 않는다.

## 2. 실제 패스 순서

```text
LX mesh skin/world producer (Shadow/GBuffer가 같은 생산을 공유)
  -> legacy Shadow + LX.Scene.Shadow (세 캐스케이드의 기존 depth 보존)
  -> legacy GBuffer (기존 depth clear와 Opaque/Masked capture)
  -> LX.Scene.GBuffer (투과 제외, 같은 5 MRT와 같은 D32, Less, depth write)
  -> Decal / SSAO / legacy Deferred
  -> LX.Scene.LookupCapture (8 + 3 MRT) / LookupBake
  -> SSS draw가 있으면 SubsurfaceReflection / Capture (7 MRT) / Filter
  -> LX.Scene.Color (같은 HDR, 같은 D32, Equal, depth write 0)
  -> 투과 draw가 있으면 HDR/D32 복사 -> TransmissionGBuffer
     -> lookup 재수집/bake -> RefractionCapture/Bake
     -> Special 반사/필요한 SSS -> TransmissionColor
  -> LX.Scene.LookupReady
  -> Skybox / SSGI / 기존 Forward / SSR / 후처리
```

- LX GBuffer는 depth를 clear하지 않는다. 먼저 기록된 legacy Opaque/Masked와 같은
  깊이 검사에 참여한다. material graph alpha와 coverage cutoff로 Masked를 평가한다.
- GBuffer bitmask에 draw별 `0x80000001..0x80001000` owner를 기록한다. legacy bitmask와
  분리되며, coplanar의 `Equal`만으로 다른 재질이 winner를 덮어쓸 수 없다.
- 컬러 패스는 같은 mesh/pose/VS/coverage를 사용하고 depth는 읽기 전용으로 바인딩한다.
  정확한 owner가 자기 draw와 일치하는 fragment만 기존 HDR을 교체한다.
  legacy 및 배경 픽셀은 유지한다.
- 컬러는 원래 graph를 다시 평가한다. 기본 GBuffer에 담지 못하는 IOR·coat·sheen·
  anisotropy·film을 flat Standard 값으로 복원하지 않는다.
- fine UV derivative는 owner 분기 전에 계산한다. 선택된 view light의 직접광,
  기존 SSAO 값, cascade shadow와 HDR environment를 소비한다.
  Layered 발광도 상위 lobe의 감쇠를 적용한다.

투과 surface는 [refraction 계약](MaterialGraphSceneRefraction.md)에 따라 완성된 불투명 배경을
읽고 최종 depth와 owner를 갱신한다. 배경 SSAO를 투과 surface에 적용하지 않는다.

Decal의 LX lobe 수정, LX mesh의 shadow caster 설치, Volume·Blended transport,
SSAO/SSR/SSGI와의 전체 Scene 시각 회귀는 별도 작업이다. 위 순서의 자원 연결만으로
해당 renderer 기능 전부를 검증했다고 주장하지 않는다.

## 3. 준비·기록·수명

1. `SelectReadyInput`에서 [비동기 generation 준비·마지막 정상 재질](MaterialGraphSceneGeneration.md)을
   선택한다. 신규 generation의 Slang은 worker에서 검증하고 shadow를 포함한 Surface Core/Layered PSO 9개,
   SSS 또는 transmission PSO 11개, 두 기능을 섞으면 13개가 모두 ready여야 선택한다.
   pending/실패 슬롯은 이전 instance·coverage와 현재 geometry를 사용한다.
2. legacy upload/IBL과 `SceneHost::PrepareResidency`의 LX texture 복사 준비 뒤
   빈 graph에 `PrepareParallel`로 native prefix를 확정한다.
3. `SceneHost::Prepare`가 ready instance의 reflected binding·PSO와 scene constants,
   current mesh producer·index upload를 준비한다. prefix 이전 upload 주소를 재사용하지 않는다.
4. Shadow/GBuffer의 최초 선언이 geometry 생산을 등록한다. 이후 선언은 같은 graph output을 읽는다.
5. 콜백은 전달된 encoder를 사용하며 immutable frame을 캡처한다. 현재 recording과
   descriptor 세대를 다시 확인하고 오래된 준비 결과를 거부한다.
6. 정확한 graph batch의 completion·티켓 성공 확인 뒤 lookup·재질 슬롯을 게시한다.
   pending 티켓은 다음 owner poll에서 확인하고 stale request의 활성 교체는 거부한다.
7. graph/submission owner가 input·generation·PSO·texture·geometry를 GPU 완료까지 유지한다.
   GPU idle 뒤 host를 해제하고 backend/cache를 정리한다.

프레임 candidate는 전체 성공 후 교체한다. frame/extent·budget·미지원 route·binding·
shader/PSO·뒤쪽 draw 실패는 이전 accepted frame을 부분 결과로 덮지 않는다.
실패한 이전 frame을 새 recording으로 다시 그려 성공처럼 처리하지도 않는다.

저작 모드에서는 exact Scene module의 VS/GBuffer PS/Color PS를 DXIL·SPIR-V로 검증한다.
두 backend의 material reflection이 제품 layout과 일치해야 active backend PSO를 만든다.
DX12/Vulkan shader/native PSO의 worker 준비·제출 성공 후 슬롯 교체는 연결 문서가 소유한다.
Scene host artifact의 자동 cooking은 MAT-7 잔여다. 초기 CS의 동기 설치 제거와
전체 cache 최적화는 별도 개선이며, 최종 실시간 시간/메모리 수용은 MAT-9에서 판정한다.
lookup의 현재/이전 payload 예산과 Special·refraction의 프레임당 예산은 각각 연결 문서가 소유한다.

## 4. 검증

`Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`는 실제
`EnhancedGBufferPass`·`EnhancedDeferredPass`와 이 host를 native D3D12 graph에 기록한다.
24×24 fixture에서 legacy/LX Opaque/Masked discard·coverage, Core/Layered overlap,
coplanar draw 순서, SSAO 값과 HDR 직접광/IBL·환경맵 없는 경우를 순차·1워커·4워커로 비교한다.
컬러 전후 depth bytes와 legacy/background HDR 보존도 검사한다.
frame/예산 실패의 accepted frame 보존과 중복/다른 graph의 컬러 선언을 거부한다.
새 texture 3개의 업로드를 취소/재시도하여 `DX12TextureCache::OnUploadAborted`의
삭제된 iterator 재사용 어설션 수정도 검증한다. Debug 어설션은 팝업 대신 스택 로그로 남긴다.

HDR target의 half 저장 오차 상한은 상대 `0.001 * max(1, abs(expected))`다.
독립 float 재질/IBL 물리 계산의 기존 정규화 `0.0001`, SRGB 절대 `0.001`,
LOD transport 절대 `0.0005` 게이트는 그대로 유지한다.

아래는 2026-09-28 설치 검증의 기준 결과다. 2026-09-29 캐시 확장 결과와 로그는
[MaterialGraphSceneLookup.md](MaterialGraphSceneLookup.md)가 소유한다. 후속 이미지별 샘플링 결과는
[MaterialGraphTextureFootprints.md](MaterialGraphTextureFootprints.md)에 기록한다.

| 검증 | 각 구성의 결과 |
| --- | --- |
| 전체 raster/material/IBL 회귀 | 830,436개 검사 · GPU 659,914성분 |
| 실제 GBuffer·Deferred·LX 컬러 합성 | 8 fixture × 순차/1워커/4워커 = 24 graph |
| LX 가시 픽셀 | 3,582픽셀, 독립 직접광/IBL 계산과 HDR 대조 |
| 준비 실패·중복/다른 graph 선언 | 120개 거부, accepted frame 보존 |
| 기존 공유 깊이·스키닝 회귀 | 공유 깊이 24 graph · 스키닝 6 graph |
| D3D12 GPU validation | WARNING 이상 0건 |

새 Scene VS/GBuffer PS/Color PS는 Core/Layered 두 프로그램에 대해 DXIL·SPIR-V
총 12개 artifact의 컴파일·reflection을 확인했다. 이는 기존 회귀 출력의 `compiled=24`와
별도이며 native 실행은 D3D12다. 실제 DX12/Vulkan Scene consumer도 두 구성에서 컴파일했다.
실행 로그는 `Build/Obj/MaterialProductProbe/raster-surface-{Debug,Release}.log`,
최종 게이트 결과는 `Build/Obj/MaterialProductProbe/scene-host-gate-final.log`다.

실제 Editor Live Tick과 native Vulkan 전체 Scene 합성은 이 D3D12 pass 검증과 별도다.
Vulkan native PSO 준비·기본 draw/readback 검증은 [Scene generation §6](MaterialGraphSceneGeneration.md)이 소유한다.
SSS의 실제 Scene transport·독립 물리/HDR 비교와 GPU 검증은
[MaterialGraphSceneSubsurface.md](MaterialGraphSceneSubsurface.md)가 소유한다.
[MaterialGraphSceneRefraction.md](MaterialGraphSceneRefraction.md)는 불투명 배경 확보·최종 투과
depth/owner·굴절 compute·HDR 합성 및 해당 native 검증을 소유한다.
[MaterialGraphSceneVolume.md](MaterialGraphSceneVolume.md)는 닫힌 균질 매질의 coefficient CS,
불투명 깊이까지의 흡수·발광·단일 산란과 굴절 ray의 내부 매질 합성을 소유한다.
MAT-7은 진행 중이며 완료 공수를 추가하지 않는다.

[MaterialGraphSceneShadowDecal.md](MaterialGraphSceneShadowDecal.md)는 그래프 Alpha를 쓰는
세 캐스케이드 shadow caster와 opaque Decal의 raw 입력·derived lobe·IBL 연결을 소유한다.
