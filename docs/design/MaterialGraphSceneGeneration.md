# Material Graph Scene generation 준비와 마지막 정상 재질

**2026-09-29 · MAT-7 완료. Core/Layered·SSS·transmission Scene host의 준비·교체 경계.**

## 1. 설치 범위

`SceneHost::SelectReadyInput`을 실제 DX12/Vulkan `EnhancedSceneRenderer`의
texture residency·native parallel prefix 이전에 연결했다. 불변 generation을 준비하는
동안 같은 Scene epoch·view·Material 슬롯의 마지막 정상 재질을 사용한다.
geometry·world·pose·camera는 현재 밀봉한 입력을 사용한다.

| 단계 | 실행 위치와 게시 조건 |
| --- | --- |
| 요청 | render owner에서 generation/backend별 중복 제거·route 검사·source 소유 |
| Slang 검증 | scheduler worker에서 VS·GBuffer·Color·Lookup0/1·Shadow VS/PS·SSS/Refraction Capture, DXIL·SPIR-V 검증 |
| layout·PSO 준비 | owner가 reflected layout과 Surface Core/Layered 9개·SSS 또는 transmission 11개·혼합 13개 PSO의 소유 descriptor 작성 (shadow 포함) |
| native PSO | DX12/Vulkan worker; owner poll은 호출당 PSO 요청 1개, pending을 기다리지 않음 |
| frame 선택 | Surface generation의 9/11/13개 PSO가 모두 ready, 아니면 같은 슬롯의 마지막 정상 instance·coverage |
| 제출 게시 | fully recorded graph의 정확한 completion·티켓 성공 확인 뒤 lookup·재질 슬롯 게시 |
| 해제 | GPU completion 이후 recording owner 회수; 새 요청의 stale 제출은 활성 슬롯 교체 거부 |

Slang 작업은 host·device·root/PSO cache를 캡처하지 않는다. source·경로·generation과
컴파일 결과를 독립 소유한다. task가 host보다 늦게 끝나도 host 메모리를 접근하지 않는다.
owner는 완료된 task만 읽으며, 이미 완료된 job/ticket의 결과 확인 외에는 기다리지 않는다.

Vulkan도 `RequestGraphics`를 override한다. bytecode·입력 element·semantic 문자열을
복사한 요청을 scheduler worker에서 생성하고, owner가 완료 결과만 native handle 표에 게시한다.
mesh transform·lookup 초기 CS 설치와 root layout 생성도 아직 owner의 동기 초기화다.
따라서 Scene의 모든 최초 준비 비용이 비동기가 되었다고 표현하지 않는다.

## 2. 슬롯·원자적 선택

producer의 `SceneMaterialSource::Capture(Material)`은 runtime Material GUID를 복사한다.
이는 graph GUID·instance 주소·mesh geometry key와 별도의 슬롯 신원이다.
동일 Material의 여러 draw는 같은 instance와 coverage를 요구하며, 충돌하면 선택을 거부한다.

슬롯 키는 `(sceneEpoch, viewId, materialSlot)`이다. 제출 성공한 instance와 coverage를
한 쌍으로 저장한다. 새 요청이 pending·실패·미설치 Blended이면 그 쌍을 선택한다.
cold 슬롯은 마지막 정상 재질이 없으므로 해당 draw만 준비 완료까지 생략한다.
다른 준비 완료 슬롯과 legacy draw는 계속 렌더한다.

- generation·layout·shader·PSO는 불변 Program에 묶인다. PSO 일부 성공은 ready가 아니다.
- 새 numeric instance는 같은 ready generation을 재컴파일하지 않는다.
- fallback에도 현재 geometry를 사용한다. 이전 frame의 upload slice를 새 recording에 재사용하지 않는다.
- 슬롯별 request revision이 바뀌면 이전에 기록한 제출의 재질 게시를 거부한다.
  그 제출의 물리 렌더와 lookup 결과는 별도로 검증할 수 있다.
- 해당 view의 Scene epoch 변경·슬롯 제거는 이전 active 재질을 제거한다.
  현재 frame이 아닌 빈 입력은 슬롯을 지우지 못한다.
- 삭제·실패·취소가 legacy 재질 변환을 의미하지 않는다. 기존 자산 변환은 이번 범위에 없다.

## 3. 제출과 수명

parallel batch의 completion과 뒤따르는 `EndFrame` completion은 서로 다를 수 있다.
live consumer는 `RHIRecordedBatch::GetCompletionPoint()`와 그 batch의
`RHISubmissionTicket`을 전달한다. 예약된 upload notification만으로 성공을 게시하지 않는다.

티켓이 pending이면 publication frame·티켓을 보관하고 다음 owner poll에서 완료를 확인한다.
그때까지 active 슬롯과 lookup history는 교체하지 않는다. 티켓 성공 후에만 완전한
lookup 기록과 정확한 recording/frame/completion을 검증하고 게시한다.
그 사이 새 요청·Scene epoch 변경이 있으면 오래된 revision의 active 교체는 거부한다.
오프스크린 순차 검증의 빈 티켓은 앞선 성공적인 CPU submission drain을 호출자의 전제로 한다.

abort는 recording owner를 제거하고 active 슬롯을 보존한다. 제출한 GPU 자원은 completion까지
graph/batch/host recording owner가 유지한다. host shutdown은 submission과 GPU를 drain한 뒤,
device보다 먼저 수행한다. worker source 작업은 자체 owner로 scheduler drain까지 유지된다.

## 4. 예산과 남은 경계

generation 준비는 host별 한 번에 Slang 작업 1개, admission 64개로 제한한다.
CPU ready Program 캐시는 32개를 넘으면 슬롯·in-flight owner가 없는 항목을 정리한다.
준비 record가 16개를 넘으면 현재 슬롯이 요구하지 않는 실패 memo를 정리한다.
이는 엄격한 LRU나 전체 native PSO 메모리 예산이 아니다.

공유 native PSO/layout handle을 임의 invalidation하면 다른 consumer도 stale이 된다.
따라서 CPU record 정리와 global native cache eviction은 분리했다. 전체 shader source
디스크 캐시·native PSO/layout eviction·메모리 수용은 후속 작업이다.

자동 Scene host/compiler 쿠킹·packaged 실행은 [MaterialGraphSceneCook.md](MaterialGraphSceneCook.md),
Volume transport는 [MaterialGraphSceneVolume.md](MaterialGraphSceneVolume.md),
LX shadow caster·Decal은 [MaterialGraphSceneShadowDecal.md](MaterialGraphSceneShadowDecal.md)의
후속 구현으로 완료했다. 실제 Editor Live Tick·Vulkan 전체 Scene 실행도
[MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)의 Debug/Release 검증으로 완료했다.
SSS의 실제 transport와 근사 한계는 [MaterialGraphSceneSubsurface.md](MaterialGraphSceneSubsurface.md)가 소유한다.
transmission/refraction의 배경·자원·계산·검증 계약은
[MaterialGraphSceneRefraction.md](MaterialGraphSceneRefraction.md)가 소유한다.
초기 CS 전체 비동기화·전역 cache 최적화는 별도 개선이며, 일반 Scene의 최종 cold/카메라 이동
시간·메모리 수용은 MAT-9에서 판정한다.

## 5. 검증

`Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`는
실제 `EnhancedGBufferPass`·`EnhancedDeferredPass`·Scene host를 사용한다.
12프레임의 generation fixture는 다음을 native D3D12에서 검사한다.

1. worker를 지연시키고 pending B·더 최신 C 요청에도 기존 A·Opaque coverage 유지.
2. 더 오래된 B가 먼저 준비되어도 현재 C 요청을 대신하지 않음.
3. C의 PSO 9개가 모두 준비된 후 정확한 instance·double-sided coverage 선택.
4. Slang 오류 및 PSO 4개 성공 뒤 5번째 실패에 기존 재질 유지·실패 memo 중복 컴파일 방지.
5. recording abort·아직 완료되지 않은 실제 제출 티켓에 active 게시 금지.
6. 기록 뒤 새 요청이 들어온 C 제출의 stale 활성 교체 거부.
7. 같은 ready C generation의 numeric instance 교체에 추가 Slang 작업 없음.
8. Scene epoch 변경 시 이전 epoch의 fallback 금지.

Slang/PSO 오류는 host 준비 경계의 테스트용 generation 사본과 cache wrapper에 주입한다.
일반 LXMC decode·cook integrity 검사는 기존 제품 회귀가 소유한다.

현재 geometry·coverage·44개 material 입력·36개 IBL 성분·HDR half 출력과 shared depth/owner를
독립 CPU 기준으로 대조한다. 기존 물리 `1e-4`, SRGB/필터 transport `1e-3`,
LOD `5e-4`, HDR half 상대 `1e-3` 게이트를 변경하지 않는다.

같은 108개 source SHA-256을 고정한 Debug/Release 빌드·native D3D12 실행이 통과했다.

| 검증 | 각 구성의 결과 |
| --- | --- |
| 전체 raster/material/IBL 회귀 | 21,079,762개 검사 · GPU 3,899,966성분 |
| 실제 GBuffer·Deferred·LX 합성 | 56 graph · 가시 45,626픽셀 · 실패 거부 312개 |
| 신규 generation 시나리오 | 12프레임 · 이전 재질 유지 7회 · abort 1회 |
| 제출 보호 | pending native ticket 1회 · stale 활성 게시 거부 1회 |
| shader 준비 | 정확한 generation 6개 · worker 실행 6회 · 실패 memo 2개 |
| native PSO 준비 | DX12 worker 실행 24회 · 일부 PSO만 성공한 candidate 게시 거부 |
| 정확한 Scene lookup | 새 적분 21,901픽셀 · 재사용 23,725픽셀 |
| 기존 물리 정규화 최대 오차 | `0.0000859499` (기준 `0.0001`) |
| D3D12 GPU validation | DebugLayer/GPUValidation on · WARNING 이상 0건 |

실행 로그는 `Build/Obj/MaterialProductProbe/scene-generation-{Debug,Release}.log`,
source manifest는 `scene-generation-source-hashes.json`, 최종 gate는
`scene-generation-gate-final.log`다. gate는 회귀 script의 최종 signature와 source drift를 검사한다.
기존 `compiled=24`는 고정 raster 회귀 artifact 수이며 신규 Scene generation 수와 합산하지 않는다.

1920×1080 희소 coverage의 기존 GPU timer는 Debug cold/warm `22.3713/3.64717 ms`,
Release `124.146/2.7024 ms`였다. GPU validation을 켠 단회 기록이며 dense Scene 또는
steady-state 실시간 성능 수용으로 사용하지 않는다. cold 변동·메모리/초기화 비용은 후속이다.
실제 Editor Live Tick과 native Vulkan 실행은 앞선 D3D12 회귀에 포함하지 않았다.

## 6. Vulkan native PSO 준비 (2026-09-29)

`VulkanPipelineCache`의 native graphics 작업은 cache/host를 캡처하지 않는다.
owned request와 native device/layout만 유지한다. layout과 device는 cache의 `Shutdown`
join 이전에 해제하면 안 된다. GPU idle·cache shutdown·device shutdown 순서를 유지한다.

- `RequestGraphics`는 Pending을 기다리지 않는다. legacy `GetOrCreate`는 같은 accepted
  작업을 동기 소비할 수 있으며, 별도의 native 컴파일을 중복 제출하지 않는다.
- 같은 descriptor의 실패는 diagnostic과 함께 memo한다. 전체 invalidation은 실패 memo도 지운다.
- 전체 invalidation은 accepted 작업을 abandoned 목록으로 옮긴다. 그 결과는 새 캐시에
  게시하지 않고 완료 확인 후 파괴한다. 기존 published handle은 generation을 올려 retire한다.
- pending·failed memo·abandoned record의 합은 64개다. abandoned 작업도 완료까지 admission을
  점유한다. 무효화는 worker를 기다리지 않으며 종료의 join만 lifecycle 경계에서 기다린다.
- graphics/compute shader module은 native PSO 생성 호출 후 성공·실패 모두 해제한다.
  worker가 공유 module 배열을 변경하지 않는다. native `VkPipelineCache`는 아직 사용하지 않는다.
- native stage는 SPIR-V 산출물의 실제 `OpEntryPoint` 이름을 사용한다. 기존의
  `VSMain/PSMain/CSMain` 고정값 때문에 `LXSceneVS` 등 생성 셰이더 설치가 실패하던 문제를 수정했다.
  stage 불일치·잘린 instruction·빈/중복 entry는 native 호출 전에 실패한다.
  이 검사는 전체 SPIR-V validator를 대신하지 않는다.

`Tools/regression/verify-material-vulkan-generation.ps1 -SkipDependencyRestore`는 source SHA-256을
고정하고 Debug/Release 엔진·probe를 빌드한 뒤 Vulkan validation layer를 켜서 실행한다.

1. worker를 막은 Pending·반복 poll에 owner 생성/대기·중복 job 없음.
2. 요청 이후 원본 bytecode·semantic·input element를 교체해도 native PSO 생성과 GPU 출력 유지.
3. invalidation 중 완료된 오래된 candidate 게시 금지·stale handle 거부·fresh 요청 복구.
4. 실제 worker의 지원하지 않는 vertex slot 실패와 동일 실패 memo의 재제출 금지.
5. 64개 admission·65번째 거부·abandoned 64개 완료/회수·종료 시 queued job join.
6. stopped scheduler 거부·재시작 후 복구·synchronous consumer의 accepted job 공유.
7. Core/Layered Scene generation 두 개가 각각 shadow caster를 포함한 9개 Ready 응답 후에만 게시됨.
   같은 descriptor는 native cache를 공유한다. 최초 검증의 8개 PSO 결과와 아래 확장 검증을 구분한다.
8. worker에서 생성된 실제 PSO로 세 번 draw하고 독립 상수 RGBA 기준으로 768픽셀·3,072성분 대조.
9. 기존 `CSMain`·생성된 이름의 compute PSO 생성/캐시 재사용·잘못된 execution model 거부.
   최종 validation은 cache와 device를 실제 shutdown한 뒤 읽는다.

최초 검증에서는 554개 source SHA-256이 유지된 Debug/Release 빌드와 RTX 4070 Ti native Vulkan 실행이 통과했다.

| 항목 | Debug / Release |
| --- | --- |
| 검사 | 9,352 / 9,198개 (poll 횟수에 따라 달라짐) |
| Scene 준비 | Core/Layered 2개 · Ready 응답 16개 · native worker 14회 |
| 주 회귀 cache | accepted request/worker 각 83회 · stale 회수 65개 |
| 실패 | graphics worker 실패 memo 1개 · compute stage 거부 1개 |
| 실제 GPU 출력 | 각 768픽셀 · 3,072성분 · 상수 기준 절대 오차 `1e-6` 이내 |
| Vulkan validation | 켬 · device 종료까지 VALIDATION/PERFORMANCE WARNING 이상 0건 |

cache의 queued shutdown·재시작 요청은 주 회귀 stats를 캡처한 뒤 별도로 검사한다.
Release 빌드에서 기존 `TypeTrait.h` C4189, Debug에서 같은 C4189와 LNK4075 경고가 남았다.
새 빌드 오류·native validation 결함은 없다. 이번 준비 회귀는 GPU 성능 수용 게이트가 아니다.

Vulkan 전체 Scene의 legacy/LX 공유 depth·조명·lookup·HDR 합성 readback, 실제 Editor Live Tick,
일반 Scene 비용·전체 cache/retirement 예산·자동 Scene host 쿠킹은 이 준비 검증의 완료 범위가 아니다.
검증 로그와 source manifest는 `Build/Obj/MaterialProductProbe/vulkan-generation-*`에 둔다.
이 절은 당시의 준비 검증 기록이다. MAT-7의 후속 제품 통합 종료 결과는
[MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)가 소유한다.

### 6.1 Shadow caster 추가 후 재검증 (2026-09-29)

569개 source SHA-256 변경 0개로 Debug/Release를 다시 빌드하고 같은 native Vulkan gate를 실행했다.
Core/Layered의 9개 PSO 준비에는 신규 `LXSceneShadowVS/PS`가 포함된다.

| 항목 | Debug / Release |
| --- | --- |
| 검사 | 14,273 / 10,727개 (poll 횟수에 따라 달라짐) |
| Scene 준비 | generation 2개 · Ready 응답 18개 · native worker 15회 |
| 주 회귀 cache | accepted request/worker 각 84회 · stale 회수 65개 |
| 실제 GPU 출력 | 각 768픽셀 · 3,072성분 · 상수 기준 절대 오차 `1e-6` 이내 |
| 실패 복구 / validation | 기대한 실패 2개 · 종료까지 WARNING 이상 0건 |

최종 로그는 `Build/Obj/MaterialProductProbe/shadow-decal-vulkan-gate-final.log`,
source manifest와 개별 빌드/native 로그는 같은 폴더의 `vulkan-generation-*`다.
이는 당시 shader/PSO 설치와 기존 baseline draw의 검증이다. 이후 native Vulkan 전체 Scene
합성은 [MaterialGraphProductIntegration.md](MaterialGraphProductIntegration.md)의 Debug/Release
6개 실제 실행 경로로 확인했다.
