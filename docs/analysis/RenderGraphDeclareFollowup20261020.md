# RenderGraphDeclare 후속: 실제 본문 비용과 작업 분배

## 결론과 현재 상태

저장된 최신 캡처에서 `RenderGraphDeclare`의 병목은 작은 metadata packet의 생성만으로 설명되지 않는다. ETW를 사용하지 않은 baseline의 Declare 평균은 **4.405760 ms**이고, 순서대로 실행되는 37개 노드 본문의 합이 **4.122460 ms, 93.57%**다. 공용 워커가 수행한 기존 packet build의 합은 Declare당 **0.135630 ms**다. 이 값은 겹쳐 실행될 수 있는 작업량이며, 이를 그대로 Declare에서 뺄 수 있는 시간은 아니다.

ETW ON 원본 678개 Declare를 노드·하위 scope와 다시 연결했다. 25,086개 named node와 같은 수의 본문이 모두 올바른 부모에 포함됐다. 중첩을 제거하면 평균 **5.096890 ms** 중 그래프 API가 **1.500037 ms**, `SharedBuild`가 **0.210163 ms**, 그 밖의 노드 본문이 **3.003421 ms**다. 따라서 이번 후속은 pass usage와 resource version 저장 방식, HZB 선언의 반복 저장·이름 생성, SSGI의 지역 컨테이너와 callback capture 복사, metadata packet의 실제 작업량에 맞춘 분배를 대상으로 한다.

**기존 엔진 시간은 변경 전 저장 캡처이며, 후속 변경의 수치는 7절의 Linux CPU fixture 결과다.** 구현과 해당 CPU 회귀 검증은 완료했다. 변경 후 Windows 실행·실제 GPU 출력·프레임 시간 측정 결과는 아직 없다. [정본 JSON](RenderGraphDeclareFollowup20261020.json)은 원본 hash, 표본 선택, 노드별 수치, 재현 가능한 분석 스크립트 원문을 포함한다. 기존 [WorkerBalance 분석](WorkerBalance20261020.md)과 그 검증 기록은 앞선 변경의 근거로 보존한다.

## 1. 비교 기준과 계측 신원

| 항목 | 값·의미 |
|---|---|
| 캡처 파일 게시 커밋 | `7ebcd2b1a2a181e88e4c2fe1c8952242242ab536` |
| 캡처가 기록한 코드 HEAD | `4eeba98f2c9ad5907debfa4eaa10b2f004afb149` |
| 이번 정적 분석의 고정 기준 | `2e08a1929ffc911610ecfb48e7be50a5a8b3d317` — PR #188의 앞선 WorkerBalance 변경 포함 |
| 구성 | CreatorEditor Debug, CreatorRobot Play, 엔티티 62개 |
| 뷰 | Scene `viewId=1`, Game `viewId=2`, 각각 1186×514 |
| 공용 워커 | 8개 |
| 그래픽 경로 | software command recording/translation wrapper를 거친 네이티브 DX12 |
| baseline 표본 | 완전한 Declare 794개, NodeBody 29,378개 |
| ETW ON 표본 | Declare 678개, 뷰별 339개, 노드·본문 각각 25,086개 |
| ETW ON 구간 | 1364.921–16377.786 ms |

`4eeba98`에는 이미 owned declaration packet 병렬화가 들어 있다. `2e08a19`의 기록 대상·translation 분배, ScenePrepare packet 준비, `HasDeclaredWriter`의 vector 복사 제거 등은 이번 원본 캡처 이후의 변경이다. 따라서 이 문서의 수치로 그 변경의 효과를 계산하지 않는다. `softwareRhi`는 소프트웨어 GPU 에뮬레이션을 뜻하지 않는다.

[source-identity.json](../../Artifacts/current-state-20261020/json/source-identity.json)에는 캡처 전에 존재한 dirty 파일 목록이 있다. `sourceUnchanged=true`는 캡처 중 변경이 없었다는 뜻이다. 이전 독립 검사에서 기록된 582개 파일 중 98개는 Git blob 원문과 같고, 445개는 일률적인 LF→CRLF 변환 후 같았으며, 39개는 동등성을 확정하지 못했다. 이번에 직접 다룬 11개 코드 파일은 모두 원문 또는 그 줄바꿈 변환으로 연결된다. 캡처 코드와 정적 기준 코드의 Git blob·SHA256은 JSON의 `integrity.immutable_code_sources`에 각각 기록했다.

전체 실행 조건과 그 한계는 [WorkerBalance20261020.json](WorkerBalance20261020.json), [measurement.json](../../Artifacts/current-state-20261020/measurement.json), [conditions/identity.json](../../Artifacts/current-state-20261020/json/conditions/identity.json)이 근거다.

## 2. 원본을 분해한 방법

분석 입력은 [baseline summary](../../Artifacts/current-state-20261020/json/baseline/summary.json), [ETW timeline metadata](../../Artifacts/current-state-20261020/json/etw/timeline-metadata.json), [RenderThread 원본](../../Artifacts/current-state-20261020/json/etw/thread-28068.json)이다. 세 파일 모두 manifest의 byte 수·SHA256과 다시 일치했다.

ETW `scopes`는 `[beginMs, durationMs, markerIndex, depth, flags, view, submission]` 형식이다. duration이 양수이고 `flags & 3 == 0`이며 ON 구간에 완전히 포함되는 Declare만 선택했다. `measurements`의 렌더 구간에 포함되는지 확인하여 뷰를 대응시켰다. Scene/Game 실행 순서는 사용하지 않는다. 시간 출력의 0.0001 ms 반올림을 고려해 **부모·자식 포함 관계에만 0.0002 ms 허용 오차**를 사용했으며, ON 구간 선택에는 이 허용 오차를 사용하지 않았다.

각 Declare 안에는 `RenderDeclare.<node>`라는 실제 이름별 scope가 있다. 노드 순번으로 이름을 추측하지 않았다. 다음 조건을 678개 전체에서 확인했고 오류는 **0개**였다.

- Declare마다 대응하는 렌더 측정 구간이 정확히 하나다.
- named node가 37개이고, 각 노드에 `RenderGraphDeclareNodeBody`가 하나씩 있다.
- 노드 본문끼리 겹치지 않는다.
- 분석한 그래프 API와 `SharedBuild` scope가 노드 본문에 포함된다.
- 그래프 API 구간 합집합과 `SharedBuild` 구간 합집합은 서로 겹치지 않는다.

그래프 API 합집합에는 `ImportBuffer`, `ImportTexture`, `CreateTexture`, `CreateBuffer`, `AdvanceVersion`, `InitializePass`, `AddPass`, `AddRepeatedPass`, `AddSplitPass` marker를 포함한다. `InitializePass`는 `AddPass` 등에, `AddPass`는 `AddRepeatedPass`에 중첩될 수 있어 **개별 duration을 그대로 더하지 않고 시간 구간의 합집합**을 계산했다. `SharedBindings`는 이미 `SharedBuild` 안에 있으므로 다시 더하지 않는다.

p50은 median이고, p95는 정렬된 실제 표본의 `min(n-1, floor(n*0.95))` 값이다. 먼저 각 Declare 또는 노드 안의 구간을 계산한 뒤 표본 전체를 집계했다. 서로 다른 marker의 p95를 더하지 않는다. baseline의 전체 raw frame을 새로 내려받아 분해한 것은 아니며, baseline은 전체 summary 집계, ETW는 ON 구간 원본 분해로 명확히 구분한다.

### 주요 원본 hash

| 원본 | bytes | SHA256 |
|---|---:|---|
| `etw/timeline-metadata.json` | 2,273,845 | `04e3c457a1e24600fe36124fc3b579df3323d1279fdcbe66c12c0a09920a1e97` |
| `etw/thread-28068.json` | 29,567,398 | `a118504b763a53a817256169da00d5cd48c9fc49fe08928e9f2e5cb431132c28` |
| `baseline/summary.json` | 1,181,955 | `b8bf26d3ffee396caff8d2bd3872c8f0d88046789545ce115dec49af2ffa1a80` |

위 경로의 공통 prefix는 `Artifacts/current-state-20261020/json/`이다. 정본 JSON의 `reproduction.source_utf8`를 `analyze_declare_bodies.py`로 저장하고, 같은 디렉터리 아래에 `json/etw/...`, `json/baseline/...` 입력을 놓아 실행하면 전체 파생 결과가 재생성된다. `integrity.full_derived_analysis_sha256`으로 그 결과를 비교할 수 있다. 분석기는 엔진 소스를 변경하지 않는다. 대용량 원본을 이번 문서와 함께 중복 저장하지 않는다.

## 3. baseline: 큰 본문과 작은 packet 작업

다음은 **ETW를 사용하지 않은 baseline 전체 summary**다. 괄호의 호출 수는 한 Declare당 평균이며, 모든 값은 scope의 inclusive 시간이다.

| 항목 | 전체 호출 수 | Declare당 평균 합 ms | 중첩 해석 |
|---|---:|---:|---|
| Declare | 794 | 4.405760 | 전체 wall, p50 4.288000 / p95 5.181900 ms |
| NodeBody (37) | 29,378 | 4.122460 | 서로 다른 노드 본문의 합, Declare 안에 포함 |
| MeshletDeclare (8) | 6,352 | 0.631525 | no-op 재진입도 포함; 아래 SharedBuild 포함 |
| SharedBuild (2) | 1,588 | 0.180656 | native upload·descriptor 준비; 아래 SharedBindings 포함 |
| SharedBindings (2) | 1,588 | 0.079341 | SharedBuild의 자식 |
| AddPass (134.5) | 106,793 | 0.441779 | InitializePass 포함, 일부 repeated pass 안에 포함 |
| InitializePass (142.5) | 113,145 | 0.174215 | 상위 AddPass 등과 더하지 않음 |
| AdvanceVersion (302) | 239,788 | 0.266295 | resource version 변경 |
| ImportTexture (113) | 89,722 | 0.225296 | 그래프 metadata import |
| ImportBuffer (91) | 72,254 | 0.173848 | 그래프 metadata import |
| CreateTexture (89) | 70,666 | 0.151206 | 이 scope는 GPU texture 생성 시간과 동일하지 않음 |

이 표 전체를 더하면 중복 계산이다. ETW 원본의 중첩 제거 결과는 다음 절에서 별도로 제시한다. `MeshletDeclare` 8회는 비용이 같은 8개의 독립 작업을 뜻하지도 않는다. 이미 shared batch가 선언된 뒤 즉시 반환하는 재진입이 섞여 있다.

기존 packet build의 공용 워커 scope 합은 **107.690300 ms / 794 = 0.135630 ms/Declare**다. owner가 실행한 packet build까지 포함하면 **0.139236 ms/Declare**다. `PacketDispatch`는 평균 2.5회로 합계 0.069031 ms/Declare, `PacketJoin`은 1회 0.065351 ms다. 이 값들 역시 중첩·동시성이 있어 하나의 절감 가능 시간으로 더하지 않는다.

실제 호출 구조는 [EnhancedLivePipelineDesc::DeclareAll](https://github.com/29thnight/CreatorEngine/blob/2e08a1929ffc911610ecfb48e7be50a5a8b3d317/Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp#L211-L250)이다. owner가 `NextDeclarationPacket()`을 호출한 다음 각 `node.declare` 본문을 순서대로 실행하고, 마지막에 packet을 합류·병합한다. 따라서 metadata packet 병렬화를 노드 본문 전체의 병렬 실행으로 설명하면 안 된다.

## 4. ETW ON: 같은 표본에서의 실제 비용 분할

ETW를 사용한 구간의 Declare는 평균 **5.096890 ms**, p50 **4.896900 ms**, p95 **6.530400 ms**다. Scene 339개의 평균은 5.211236 ms, Game 339개는 4.982545 ms다. baseline 4.405760 ms와의 차이를 코드 성능 회귀로 해석하지 않는다. 서로 다른 계측 모드·표본이다.

아래 네 행은 같은 678개 Declare를 겹치지 않게 분할한 값이므로 평균끼리 더할 수 있다.

| 구성 | 평균 ms | Declare wall 비중 |
|---|---:|---:|
| 그래프 API 구간 합집합 | 1.500037 | 29.43% |
| SharedBuild 구간 합집합 | 0.210163 | 4.12% |
| 나머지 노드 본문 | 3.003421 | 58.93% |
| 노드 본문 밖 Declare 구간 | 0.383269 | 7.52% |
| **전체 Declare** | **5.096890** | **100%** |

노드 본문 합은 4.713622 ms, 전체의 92.48%다. **나머지 본문 3.003421 ms는 allocator 시간이나 병렬화 가능한 시간으로 측정된 값이 아니다.** 별도 marker가 없는 루프·검증·handle 조회·usage 정리·lambda 및 소유권 처리·이름 생성·계측 진입과 종료 등이 함께 포함된다. 이를 전부 제거할 수 있다고 가정하지 않는다.

OS 상태와 교차한 기존 결과에서 Declare 평균 5.096890 ms 중 Running은 5.035453 ms, 약 98.79%다. 따라서 이 캡처의 큰 Declare 비용을 워커 완료 대기로 설명할 근거는 없다. 원본 상태와 재계산 방법은 [WorkerBalance 분석](WorkerBalance20261020.md#etw-on에서-확인한-소유자-실행대기)에 기록돼 있다.

### 실제 이름별 상위 노드

baseline은 각 794회, ETW ON은 각 678회다. 두 평균을 한 코호트처럼 합치지 않는다. 오른쪽 두 열은 ETW 본문 안에 포함되는 비용이며 node 평균에 더하는 값이 아니다.

| 노드 | baseline 평균 ms | ETW 평균 ms | ETW p95 ms | ETW API union ms | ETW SharedBuild ms |
|---|---:|---:|---:|---:|---:|
| METEOR.Scene.Cull.First | 0.586644 | 0.670351 | 0.945100 | 0.164955 | 0.116284 |
| METEOR.Shadow.Cull.Final | 0.419632 | 0.474910 | 0.686800 | 0.160469 | 0.093879 |
| SSGI | 0.328272 | 0.398252 | 0.530500 | 0.101148 | 0 |
| METEOR.Shadow.Occluder | 0.360115 | 0.397738 | 0.589000 | 0.132473 | 0 |
| METEOR.Shadow.Raster | 0.317892 | 0.367239 | 0.544300 | 0.130280 | 0 |
| METEOR.Shadow.History | 0.285480 | 0.350191 | 0.473900 | 0.171242 | 0 |
| METEOR.Scene.Occluder | 0.235956 | 0.273320 | 0.410800 | 0.088366 | 0 |
| METEOR.Scene.Visibility | 0.241787 | 0.268817 | 0.396300 | 0.088596 | 0 |
| METEOR.Scene.Geometry | 0.204112 | 0.230503 | 0.325700 | 0.103777 | 0 |
| METEOR.Scene.Cull.Final | 0.181737 | 0.217594 | 0.333500 | 0.064442 | 0 |

전체 37개 노드와 뷰별 분포, 세부 API 호출 수는 JSON의 `etw_nodes`에 있다. 이 표는 GPU pass 실행 시간이 아니라 CPU 선언 시간이다.

## 5. 코드까지 연결되는 큰 본문 경로

### 5.1 이전 HZB import: 첫 MeshletDeclare보다 앞선 비용

`METEOR.Scene.Cull.First`는 shared first-stage 선언을 통해 camera와 최대 세 shadow view의 history pyramid를 먼저 import한다. 경로는 [SceneHost::DeclareSharedFirst](https://github.com/29thnight/CreatorEngine/blob/2e08a1929ffc911610ecfb48e7be50a5a8b3d317/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L3736-L3776) → [Frame::DeclareFirstStageBatch](https://github.com/29thnight/CreatorEngine/blob/2e08a1929ffc911610ecfb48e7be50a5a8b3d317/Engine/RenderEngine/GpuGeometryVisibility.cpp#L1334-L1346) → [Pyramid::ImportHistory](https://github.com/29thnight/CreatorEngine/blob/2e08a1929ffc911610ecfb48e7be50a5a8b3d317/Engine/RenderEngine/GpuGeometryOcclusion.cpp#L330-L349)다.

| Scene.Cull.First 내부 구간 | 평균 ms | p95 ms |
|---|---:|---:|
| NodeBody 시작 → 첫 MeshletDeclare 시작 | 0.296368 | 0.449200 |
| 그 안의 ImportTexture 합집합 | 0.093367 | 0.126600 |
| 그 구간의 나머지 비용 | 0.203000 | 0.314400 |
| 첫 MeshletDeclare 자체 | 0.356475 | 0.525200 |

678개 **모든 표본에서 첫 MeshletDeclare 이전에 정확히 44번의 ImportTexture**가 있다. 위쪽 첫 행은 아래 두 행의 합으로 분해되지만, 첫 행과 아래 두 행을 모두 더하면 안 된다. 첫 MeshletDeclare에는 SharedBuild와 그 이후 graph metadata 작업이 포함된다.

고정 기준 코드의 `ImportHistory`는 mip마다 `m_history->name + ".PreviousHZB"`를 만들어 넘기고, recording-local `Pyramid::m_levels` vector에 push한다. `ReadHistory`가 새 Pyramid를 만들기 때문에 이 vector의 이전 recording 용량을 재사용하지 않는다. `RequireImportedFinalState`는 별도 marker 없이 graph의 retained final-state 목록에 추가한다. 이 코드들이 0.203000 ms 잔여 구간에 있다는 것은 확인되지만, 각각의 비용이나 allocation 횟수를 원본에서 따로 측정한 것은 아니다.

이번 HZB 변경 대상은 최대 16개 level handle의 고정 저장소와 previous-HZB 이름 재사용이다. depth/view/recording/descriptor/graph epoch 검증, history의 읽기 전용 계약, import된 final state는 유지해야 한다. `ShadowFinalCull.HzbDeclare`, `Shadow.History` 및 camera history도 같은 pyramid 선언 코드를 사용하므로 회귀 검증은 history import와 current-depth 선언을 함께 포함해야 한다.

### 5.2 SSGI: 선언 중 지역 vector와 capture 복사

고정 기준 [EnhancedSSGIPass::Declare](https://github.com/29thnight/CreatorEngine/blob/2e08a1929ffc911610ecfb48e7be50a5a8b3d317/Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp#L398-L992)는 mip마다 두 usage를 채우는 지역 vector를 만들고, Trace·Resolve·Filter·Composite·StoreHistory에서도 별도 usage/SRV 목록을 만든다. 공통 `declareStage`는 지역 `constantCopy` vector를 생성한 뒤 SRV 목록과 함께 callback에 값으로 복사한다. 주요 위치는 532, 601, 705–718, 815–861, 873–886, 901–934, 958–977행이다.

ETW의 SSGI node 평균 0.398252 ms 중 NodeBody는 0.395047 ms다. API 합집합은 0.101148 ms이고 그 밖의 본문은 0.293899 ms다. 매 노드의 호출 수는 AddPass 13, InitializePass 13, AdvanceVersion 14, CreateTexture 12, ImportTexture 4다. 이 역시 0.293899 ms 전체가 vector 생성 비용이라는 증거는 아니다. 다만 크기가 제한된 입력에서 불필요한 동적 저장·복사 경로가 본문에 직접 남아 있어 좁은 변경과 byte/metadata parity 검증으로 접근할 수 있다.

새 저장 방식은 SRV 순서와 누락된 normal/motion/material 입력의 fallback을 보존해야 한다. callback은 Declare 이후에 실행되므로 지역 배열을 가리키는 span만 capture하면 안 된다. 상수와 handle은 callback 자체가 소유하거나, 제출 완료까지 살아 있는 명확한 소유자의 저장소에 있어야 한다.

같은 함수의 `UploadConstants`, `CreateBindings`, `Dispatch` 중 execute lambda 안의 호출은 **기록·실행 단계**다. 이를 Declare 개선의 측정 근거에 더하지 않는다. callback 안에 있는 임시 source vector도 선언 시간 분석에서는 별개로 구분한다.

### 5.3 여러 노드에 공통인 graph 저장 비용

[Graph API](https://github.com/29thnight/CreatorEngine/blob/2e08a1929ffc911610ecfb48e7be50a5a8b3d317/Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp#L755-L1329)는 pass usage, resource version, 이름 및 선언 packet metadata를 만든다. baseline만 보더라도 AdvanceVersion은 Declare당 302회, InitializePass는 142.5회다. 따라서 고정된 노드 한 개만 worker로 옮기기 전에 공통 저장 방식의 반복 비용을 줄일 이유가 있다.

이번 계획은 pass usage를 소유권이 분명한 immutable arena span으로 보관하고 trivial 복사를 사용하며, resource version은 retained storage와 실제 활성 범위를 분리하는 것이다. graph epoch, index/version identity, 순서, 실패 시 상태, packet 작업이 읽는 주소의 안정성과 완료 전에 저장소를 재사용하지 않는 조건이 검증 대상이다. Debug의 checked-container bookkeeping 감소 가능성은 있지만, 현재 캡처만으로 그 개선량을 확정하지 않는다.

## 6. 분배 구조의 범위와 제외한 방법

이번 metadata packet 분배는 실제 사용 가능한 worker 수를 최대 8개까지 반영하고, payload 비용을 기준으로 최대 16개 범위로 나눈 다음 큰 범위부터 작업자가 동적으로 가져간다. 한 pass의 phase 저장소는 한 작업이 소유한다. 필요한 이름·descriptor·phase 변경이 없는 warm 묶음은 건너뛰고, 작은 작업은 arena batch를 만들지 않고 직접 처리한다. 최종 merge의 선언 순서와 callback/resource 수명은 유지한다.

admission은 **합계 8,388,608 비용점수 이상, worker당 최소 524,288점수**로 제한한다. 이름 또는 descriptor 변경에는 고정 64와 문자열 byte 수를, repeated phase에는 64와 이름·usage byte 수를 더하는 추정치다. 따라서 이 수치를 실제 memcpy byte, allocation byte 또는 측정 대역폭으로 부르지 않는다. 작은 packet을 실제 scheduler에 제출했을 때의 비용을 고려한 보수적 기준이며, 최종 CPU fixture 결과와 Windows 성능 수용은 구분한다.

이것은 모든 `node.declare`를 동시에 호출한다는 뜻이 아니다. 현재 node callback은 동일한 graph의 resource/pass/version 목록을 변경하고, 같은 frame/stage의 handle을 게시하며, 앞 노드가 만든 version을 다음 노드가 검사한다. 단순한 병렬 for는 이 계약을 보존하지 않는다. 독립적인 owned payload 준비를 병렬화하고 graph에 대한 게시 경계를 유지하는 편이 현재 구조에 맞는다.

작은 `SharedBuild`를 별도 job으로 옮기는 방식도 이번 계획에서 제외한다. [10월 19일의 별도 CPU 실험](RenderGraphDeclareParallel20261019.md)에서는 64-bin visibility 작업의 직접 실행 약 4.2 µs가 실제 scheduler를 경유하면 1/4/8-worker에서 약 41.5/46.2/48.6 µs로 늘었다. 이것은 당시 portable fixture 결과이며 이번 캡처의 0.180656 ms에 그대로 적용할 수 있는 예측식은 아니다. 실제 scheduler 비용과 입력 크기별 교차점을 확인하지 않고 작은 leaf를 더 잘게 제출하지 않는 근거로만 사용한다.

`GpuVisibility.PrepareInternal`, draw/material bindings 준비, HZB texture 준비 중 ScenePrepare에서 수행하는 작업은 이번 Declare 시간에 포함시키지 않는다. 이번 후속에서 ScenePrepare나 Execute의 비용을 Declare에서 제거한 것으로 보고하지 않는다.

## 7. 구현·검증 기록

최종 소스에 대한 CPU 검증을 완료했다. Windows·GPU 실측은 아래와 같이 미실행으로 남아 있다. 검증 JSON에는 실행에 사용한 source hash를 기록하며, 이후 소스가 바뀌면 기존 결과를 새 바이트의 검증으로 취급하지 않는다.

| 변경 단위 | 현재 상태 | 검증 범위·남은 확인 |
|---|---|---|
| Pass usage immutable arena span·resource version retained storage | 일반 및 ASan+UBSan 통과 | 각각 148,142 checks; 선언·cache 본문, 성장·reset·epoch·atomic flag 초기화·부분 실패·비자명 객체 파괴 |
| SSGI bounded usage/SRV/상수 소유 | 일반 및 ASan+UBSan 통과 | 812개 시나리오, 각각 565,658 checks; metadata·상수 bytes·fallback slot·지연 callback 소유권 비교 |
| HZB 16-handle 저장·이름 재사용 | 일반 및 ASan+UBSan 통과 | 232 parity 시나리오, 각각 63,595 checks; 1–16 levels·history/current·view/recording/graph·반복/부분 선언 |
| Packet 비용 기반 분배·dynamic claim | 일반 및 ASan+UBSan 통과 | worker 1/2/4/8; 공통 정확성 130,280 checks, 일반 보고서는 timing 후 검증 포함 145,772 checks |
| 공유 RenderArena 변경의 기존 ScenePrepare 연동 | 일반 및 ASan+UBSan 통과 | 각각 308,808 checks; 기존 suite의 연동 회귀 확인 |
| Include 경계 검사 | 기존 FAIL 유지 | 같은 checkout 범위에서 상향 27·물리 경계 69, stdout 동일·새 위반 0; 규칙 변경 없음 |
| Windows Debug·Release 호스트 빌드 | 미실행 | 대상 엔진 toolchain에서 실제 빌드 |
| 네이티브 GPU 출력·동기화·수명 | 미실행 | history/current HZB와 SSGI 결과, backend 검증 |
| 변경 후 프레임 성능 | 미측정 | 같은 장면·입력·화면 크기·worker 수·계측 모드의 반복 A/B |

### SSGI CPU 회귀·할당 관측

[verify-ssgi-declaration-cpu.py](../../Tools/regression/verify-ssgi-declaration-cpu.py)는 고정 `2e08a19`와 변경본의 실제 SSGI class·parameter·Declare·ResetHistory 본문 및 graph 소유권 template을 추출한다. 실제 `RenderArena`를 컴파일하고 graph/RHI 경계는 선언·업로드·binding·command를 기록하는 fixture로 대체한다. 정상과 fail-fast ASan+UBSan에서 각각 **812개 시나리오 / 565,658 checks**를 통과했다. 세 graph mode, 256개 optional-input 조합, mip 1–8, temporal/history, arena 성장, upload/binding 실패를 포함한다. LeakSanitizer는 비활성화했으며 이를 누수 검사 통과로 표시하지 않는다.

별도 warmed allocation 조건은 **1186×514, mip 8, 모든 optional 입력, 16회 warmup 후 17회 표본**이다. 모든 표본에서 Declare의 heap 요청이 **76→5회, 93.42% 감소**했고, 지연 callback의 임시 staging 요청은 **11→0회**였다. `operator new/new[]`와 실제 arena의 `mi_malloc`/`mi_malloc_aligned` malloc-shim 호출을 함께 센다. graph/trace 저장소와 arena는 warm 재사용한다. Configure/Reset을 포함한 전체 프레임 allocation, 실제 driver allocation, Windows 실행 시간 또는 GPU 시간의 결과가 아니다. Declare의 나머지 5회까지 제거했다는 주장도 하지 않는다.

정본 JSON의 `verification.executed_results`에 원본 보고서 hash, 비교 기준, 추출 본문 hash, compiler/sanitizer 및 결과를 수록했다. SSGI CPP의 검증 바이트 SHA256은 `f2ab5f2db18863047acc7641169a305e3be7c5e818d2eba801d32be72e5be350`이다. 최종 보고서 10개에 기록된 입력 hash 비교 102개가 현재 소스와 모두 일치했다. 이는 중복 입력을 포함하는 비교 수이며, 서로 다른 파일 102개를 뜻하지 않는다.

### HZB CPU 회귀·할당 관측

[verify-hzb-declaration-cpu.py](../../Tools/regression/verify-hzb-declaration-cpu.py)는 고정 `2e08a19`와 변경본의 실제 class/layout 및 `ReadHistory`, `ImportHistory`, `Declare`, `AddReadUsages`, `BindingInputs`, `Bindings`, view/epoch 검사를 추출한다. graph/RHI 서비스, pipeline 초기화와 GPU retirement가 fixture 경계다. 일반 및 ASan+UBSan에서 각각 **232 parity 시나리오 / 63,595 checks**를 통과했다. 빈 history, 16단계 초과 또는 reader 생성 후 크기가 바뀐 history는 새로 명시적으로 거부하는 입력으로 따로 확인한다. 실제 native graph나 GPU shader를 실행한 검사는 아니다.

**16 levels, 16회 warmup 후 17회 표본**에서 `ReadHistory`의 heap 요청은 **3→3회**, `ImportHistory`는 **21→0회**, 두 구간의 합은 **24→3회**였다. 실제 추출 본문의 `new/new[]/aligned new`를 세며 graph trace 저장소는 미리 준비한다. 이전 HZB 이름을 reader에서 한 번 소유하는 준비 비용은 남아 있다. 이 16-level fixture를 실제 캡처의 네 pyramid·44 mip에서 관측한 allocation 수로 바꾸어 쓰지 않으며, 전체 프레임 무할당이나 GPU 시간 개선도 주장하지 않는다.

### Graph 저장소 CPU 회귀·할당 관측

[verify-render-graph-declaration-storage.py](../../Tools/regression/verify-render-graph-declaration-storage.py)는 `2e08a19`와 변경본의 실제 선언·cache 본문, production 타입 및 실제 arena를 비교한다. CPU RHI/profiling/job 서비스와 mimalloc의 경계는 fixture/shim이며, 실제 native command recording 검사는 아니다. 일반 및 ASan+UBSan에서 각각 **148,142 checks**를 통과했다. 저장소 성장 후 span 주소, reset/epoch, cached declaration의 소유권, 이전 atomic flag 제거, 부분 실패 및 비자명 객체의 destructor 호출을 확인한다.

warm 상태의 **128 passes × pass당 8 usages**에서 lvalue 전달의 heap 요청은 기존과 변경본이 모두 **0회**다. rvalue 전달의 요청은 **128→0회**다. 이 둘을 합쳐 모든 호출 방식에서 할당을 줄였다고 쓰지 않는다. 변경본 arena의 usage payload 사용량은 **24,576 bytes**이며, 매번 그만큼을 새로 heap에 할당했다는 뜻은 아니다. rvalue 입력의 용량을 호출자에게 남겨 재사용하고, graph가 소유한 arena에 값을 복사하는 계약이다. 이 관측 역시 CPU fixture의 요청 수이며 Windows Debug STL 비용이나 프레임 시간의 실측 개선량은 아니다.

### Packet 분배 CPU 결과와 검증 범위

[verify-render-graph-packet-dispatch.py](../../Tools/regression/verify-render-graph-packet-dispatch.py)는 실제 packet 본문과 `JobScheduler`·`ThreadPool`·enkiTS를 실행한다. Resource/Pass는 metadata projection이며, 전체 RHI 구현은 아니다. worker 1/2/4/8, merge 순서·작은 작업·반복 처리·취소·실패 시 drain을 확인했다. 일반 및 ASan+UBSan의 공통 정확성 검사는 **130,280 checks**다. 일반 보고서의 **145,772 checks**에는 다음 timing 뒤의 출력 검증이 추가돼 있다. 두 보고서의 입력 hash 13개는 동일하다.

다음은 GCC 13.3 `-O2`의 **packet publish CPU fixture median(µs)**이며 전체 Declare나 프레임 시간이 아니다. 각 variant는 5회 warmup 후 19회 측정했고 baseline→변경본→강제 inline 순서로 실행했다. 교차·무작위 실행이 아니므로 작은 차이는 잡음의 영향을 받는다. 준비된 scene metadata는 측정 밖이고, publish의 begin/next/finish 및 join이 측정 안에 있다.

| 입력 | baseline µs | 변경본 µs | 변경본 강제 inline µs | jobs | 별도 재생에서 관측한 worker 수 |
|---|---:|---:|---:|---:|---:|
| warm_names | 228.394 | 1.653 | 1.042 | 0 | 0 |
| small_phases | 206.412 | 4.647 | 4.738 | 1 | 0 |
| medium_phases | 59.139 | 4.707 | 4.917 | 1 | 0 |
| medium_256_phases | 70.416 | 19.780 | 19.579 | 1 | 0 |
| medium_1024_phases | 152.710 | 71.858 | 74.111 | 1 | 0 |
| medium_2048_phases | 225.090 | 192.260 | 225.610 | 1 | 0 |
| skewed_phases | 1042.930 | 319.562 | 892.766 | 8 | 7 |

`jobs=1`은 owner가 직접 처리했다는 뜻이다. 강제 inline은 pool worker에서 같은 경로를 호출하여 nested-worker fallback을 유도하며, 바깥 job의 제출·합류는 측정하지 않는다. 따라서 호출 스레드가 다르다. worker 수는 timing 후 **별도 1회 재생**에서 관측한 값이다. skewed 입력의 7 workers를 전체 19개 표본이나 항상 8 workers를 활용했다는 근거로 확대하지 않는다. 합계 8,388,608점수의 admission은 작은 작업의 제출 비용을 피하기 위한 보수적 CPU fixture 기준이며 Windows Debug의 최적값으로 확정한 수치가 아니다.

기존 [ScenePrepare suite](../../Tools/regression/verify-scene-packet-preparation.py)도 공유 `RenderArena` 변경 뒤 일반 및 ASan+UBSan에서 각각 **308,808 checks**를 통과했다. 이 재실행은 연동 회귀 확인이며, 보고서 안의 이전 ScenePrepare 할당 개선을 이번 후속의 새 성과로 계산하지 않는다. CPU 검증 결과 원문과 hash는 정본 JSON에 수록했다. ASan+UBSan의 LeakSanitizer는 비활성화했으므로 누수 검증 통과를 뜻하지 않는다.

Include 검사 결과는 **기존 FAIL**이다. 같은 checkout 범위에서 상향 27·물리 경계 69가 유지됐고 stdout SHA256 `9955a04f95e53baf1f631328f9f46e4ce17f1f06046ed499caa16fa360fe5b76`이 앞선 검사와 같다. 규칙·allowlist는 변경하지 않았다. 전체 HEAD의 물리 경계 70과의 차이는 기존 checkout에 없는 ThirdParty 프로젝트 한 개이며, 상세 비교는 JSON의 `verification.include_boundary`에 보존했다.

CPU-only fixture를 통과하더라도 실제 Windows/GPU 검증을 대체하지 않는다. 속도 비교에는 Declare 전체, 상위 named node, packet dispatch/join, allocation 및 raw bytes의 의미를 함께 확인해야 한다. 계측 marker의 개수나 경계를 바꿨다면 작은 scope 시간 감소를 순수 본문 speedup으로 단정하지 않는다. validation/readback을 켠 진단 실행과 일반 프레임 성능 캡처도 구분한다.

사용자가 지정한 [cpu-performance-engineering](https://github.com/usamahz/cpu-performance-engineering)의 측정·동시성·allocator 관점을 이어받았다. 전용 MCP는 호출 가능한 도구로 노출되지 않아 공식 README를 참고했으며, 외부 코드를 복제하지 않았다. 이번 문서는 누적 작업량과 critical path, 정적 개선 가능성과 실측 결과, 고정 저장소와 제출 완료 수명을 분리해 기록한다.
