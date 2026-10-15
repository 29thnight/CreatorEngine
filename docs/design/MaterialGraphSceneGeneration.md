# Scene program 준비, 선택, generation 수명

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 세 가지 게시 경계

저작 IR 게시, `GenerationStore`의 검증된 CPU product 게시, `SceneHost`의 native program/프레임 게시를 구별한다. 앞 단계가 성공해도 다음 단계가 준비 중이거나 실패할 수 있다. last-valid 보존은 실패 시 accepted owner를 훼손하지 않는 계약이지 아무 frame에 오래된 owner를 끼워 넣는 정책이 아니다.

`GenerationStore`의 loader는 mutex 밖에서 실행된다. BeginPreparation의 ticket은 reload/remove/clear로 무효화되며 Publish에서 재검사한다. digest가 같은 결과는 owner를 보존할 수 있다. 격리된 편집 preview는 expected accepted owner와 비교해 적용하며 경쟁 편집을 덮지 않는다.

## 비동기 program 준비

`SceneHost::RequestProgram`은 exact Generation과 backend에 대한 준비를 요청한다. scheduler worker의 검증/복구 결과는 `PollPrograms`에서 native graphics/mesh pipeline 요청으로 이어진다. native PSO도 준비 상태를 poll하며 shader bytecode, entry/profile, reflection/layout, fixed-state와 generation을 함께 유지한다.

현재 Scene target은 VS/PS 외 mesh visibility/reconstruction/shadow stage를 포함한다. `SceneCompileTargets`에서 필요한 집합을 산출하고 `LoadSceneShaders`는 cooked host identity와 완전한 target 집합을 검증한다. Editor는 실행 backend만 cook할 수 있으며 packaged product의 양 backend 준비는 cooker 계약이다.

## 슬롯·원자적 선택

slot key는 scene epoch, view ID, material slot이다. 한 slot은 requested instance와 coverage, selection revision을 보관한다. 현재 view에서 사라진 slot과 오래된 epoch를 정리한다.

`SelectReadyInput`은 먼저 view와 모든 slot identity를 검증하고 모든 requested program이 ready인지 확인한 다음 selected view를 복사한다. 하나라도 pending이면 `SelectionDeferred()`를 세우고 실패 반환으로 프레임을 보류한다. 알려진 preparation error는 그대로 반환한다. 이전 instance 또는 native material로 일부 draw만 대체하지 않는다.

## 제출과 자원 회수

program cache entry 정리는 cache의 CPU pin만 해제한다. graph callback, active/submitted frame, 다른 cache holder가 가진 native handle을 refcount만 보고 무효화하면 안 된다. `LX::Runtime::GraphicsGeneration`은 request와 bytecode/state를, native pipeline cache는 PSO 저장소를 소유한다.

submission ticket 접수, native 성공, GPU completion은 서로 다른 증거다. `PublishSubmittedCache`와 upload transaction callback은 실패·abort·완료에 따라 보존/게시/회수를 나눈다. submitted 실패 후보도 GPU가 읽을 수 있으면 completion까지 유지한다. 종료는 graph/submission을 drain한 뒤 device보다 먼저 host를 종료한다.

## 진단과 남은 검증

`ProgramStats`의 submissions/worker executions/failures/publications/pending/ready는 작업 상태이며 frame rate나 GPU-visible draw 수가 아니다. geometry/shadow submission counters도 CPU logical command, indirect 제출, GPU survivor를 구분한다.

reload 중 backend 누락, incomplete stage, stale ticket, PSO 실패, graph submission 거부, shutdown 중 worker 완료를 별도 회귀로 확인해야 한다. 이 문서는 현재 분기와 소유권을 정적으로 확인했으며 최신 실행 PASS는 선언하지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphRuntime.h`](../../Engine/RenderEngine/MaterialGraphRuntime.h)
- [`Engine/RenderEngine/MaterialGraphRuntime.cpp`](../../Engine/RenderEngine/MaterialGraphRuntime.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/LXMaterialPipeline.h`](../../Engine/RenderEngine/LXMaterialPipeline.h)
