# 애니메이션 실행·품질 예산과 Graph 후속 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 실행 경로

SceneManager가 AnimationScheduler를 소유한다. `SceneManager::GameLogic`은 Scene::Update→YieldNull→InternalAnimationUpdateEvent→Scene::LateUpdate 순으로 실행한다. scheduler는 내부 animation event에서 포즈를 계산한다. AnimatorSystem의 controller/state 전이 갱신과 scheduler의 포즈 수학은 같은 함수가 아니다.

Animator는 Scene 컴포넌트이며 OnAddedToScene/OnRemovingFromScene에서 scheduler에 등록/해지한다. scheduler 등록부의 raw pointer는 소유 포인터가 아니다. 등록과 Update는 owner thread에서 실행하고 Update가 모든 작업자를 join한 후 반환하므로 그 프레임 안에서만 유효하다. 단순 mutex가 컴포넌트 수명을 작업자까지 보장하는 것은 아니다.

## 한 번의 Update

1. owner가 Animator 목록과 정확한 animation binding을 capture한다. skeleton/clip generation을 작업자 중간에 latest로 다시 찾지 않는다
2. 화면 가시성·projected height·camera 유무·editor preview·필수 제약을 관찰하고 품질 후보를 정한다
3. preparation job group이 재생 시간, 이벤트 구간, frame task recipe를 준비한다
4. 첫 group을 join한다. preparation 실패이면 다음 pose group을 제출하지 않는다
5. CPU 예산과 recipe 비용으로 quality를 확정하고 실행 group을 제출한다. 단일 chunk는 inline으로 처리해 즉시 wait할 worker wake를 피한다
6. 두 번째 group을 join하고 비용 모델/HUD snapshot을 게시한다
7. owner가 Scene::PublishAnimatorPose와 socket attachment를 반영한다. worker는 Scene storage에 직접 쓰지 않는다

새 binding이 아직 ready가 아니면 이전 pose 배열을 새 bone 이름 아래에 게시하지 않고 마지막 완료 상태를 유지한다. Update 끝에 capture를 놓으며 다음 프레임까지 borrowed view를 보존하지 않는다.

## 공유 자산과 가변 인스턴스

skeleton/clip generation은 불변 자산이고 시간·cursor·transition·포즈는 AnimatorSystem의 instance/controller playback 상태다. decode storage 공유는 [ModelAnimationStorageSharing](../design/ModelAnimationStorageSharing.md)에 따르며 animation task recipe는 저장 Graph asset이 아니다.

AnimTaskList는 선행 작업 인덱스와 output 도달성을 가진 평면 실행 레시피다. sampling, blend, additive/mask, FK, two-bone IK, bone transform, output을 기존 pose pool과 palette에 연결한다. 모든 bone Entity를 매 프레임 일반 Transform처럼 갱신하는 대신 관측된 bone을 물질화하고 필요한 Scene publication을 수행한다.

## 시간과 이벤트

AdvanceClip은 wrapped sample time과 full event interval을 따로 보존한다. forward는 (begin,end], reverse는 [end,begin)이며 loop 경계를 넘는 여러 주기도 순회한다. 같은 key는 저작 순서가 tie breaker다. key 1은 끝나는 cycle, key 0은 다음 cycle의 시작이다. 유효하지 않은 시간/duration은 진행 값으로 그대로 전파하지 않는다.

clip event는 포즈 계산과 별도로 시간 구간에서 수집해 기존 관리 message 경계로 보낸다. frame당 wrapped progress만 비교하면 다중 loop·역재생 이벤트를 잃을 수 있다. layer가 bone channel을 제외하면 이전 local pose를 보존하되 parent 변환은 따라간다.

## L0~L7과 예산

기본 quality 선택은 editor preview 또는 camera 없음이면 L0, invisible이면 L7이다. visible projected height의 경계는 .12/.08/.05/.035/.02/.01이며 hysteresis가 인접 단계의 진동을 줄인다. 선택은 required IK/bone transform 같은 제약도 고려한다.

낮은 품질은 optional procedural correction, overlay layer, transition recipe, sampling bone 범위와 frame 간 평가 빈도를 조정한다. L5에서 샘플하는 bone 수를 줄여도 FK/publication은 전체 skeleton을 따라 socket/skinning 의미를 유지한다. L6는 평가하지 않는 프레임에 보간을 수행하고 L7의 예산 추정은 0이다.

비용 모델은 task kind당 evaluated bone 기준 측정과 지수 평균을 사용한다. 8회 이후 극단 sample을 제한하고 promotion grace/hysteresis로 승격을 안정화한다. 이 모델은 wall-clock hard deadline 보장이 아니다. worker 대기·게시·socket 비용과 recipe 비용을 분리해서 읽는다. HUD는 요청 후 500ms의 유효 창을 가지며 UI가 Animator를 다른 스레드에서 직접 읽지 않는다.

## 아직 구현되지 않은 Graph 확장

기존 확장 의도 AG0~AG5는 유지한다. 현재 `AnimationState::AnimationIndex`와 controller 저작 구조는 불변 AnimationGraphAsset으로 대체된 상태가 아니다. C# Animator의 이름 기반 parameter API를 유지하면서 State가 rootPoseSource를 가리키게 하는 것이 목표다.

- AG0: 현재 Scene/Prefab/C#/Editor·자산 신원·LOD/event/socket 기준선과 node ID/실패 정책 확정
- AG1: Clip PoseSource와 기존 AnimationIndex reader adapter를 통해 같은 task recipe 생성
- AG2a/AG2b: 불변 Graph 정의의 저장/cook·stable ID와 기존 AnimInstance의 node/FSM runtime slot·재바인딩 연결. 별도 pose/time 소유자를 만들지 않는다
- AG3: typed ParameterId와 compile된 전이 조건, rename/type mismatch 진단. 외부 문자열 API 호환
- AG4a~c: Blend1D/2D, LayerBlend/BoneMask/Additive, Slot/one-shot/crossfade를 기존 executor에 연결
- AG5: PoseCache·일반 IK·Root Motion·Notify의 계약 및 비용 근거를 확정한다. 기능 구현 자체와 VM 착수는 별도다

bytecode VM, full graph editor, 일반 IK/root motion 구현, Vulkan 교차 수용을 현행 기능이나 자동 완료로 표시하지 않는다. 과거 36인일은 당시 범위의 추정이며 현재 일정 약속이 아니다. 새로운 frontend는 L2/L3 pruning·L4 blend snap의 기존 recipe shape 의존을 재검토해야 한다.

## 검증 경계

`AnimationPlaybackSelfTest.cpp`와 관련 회귀 도구는 loop/reverse/event, mask/additive, LOD, socket, generation 교체, DDOL, 제품 프레임을 점검하는 입력이다. 이번 개정에서는 실행하지 않았다. 이전 golden digest·100체 결과·개선 배수를 지금의 성능으로 재인용하지 않는다.

## 구현 근거

- [Engine/SceneRuntime/AnimationScheduler.h](../../Engine/SceneRuntime/AnimationScheduler.h)
- [Engine/SceneRuntime/AnimationScheduler.cpp](../../Engine/SceneRuntime/AnimationScheduler.cpp)
- [Engine/SceneRuntime/AnimationPlayback.h](../../Engine/SceneRuntime/AnimationPlayback.h)
- [Engine/SceneRuntime/AnimationBudget.h](../../Engine/SceneRuntime/AnimationBudget.h)
- [Engine/SceneRuntime/AnimTaskList.h](../../Engine/SceneRuntime/AnimTaskList.h)
- [Engine/SceneRuntime/AnimatorSystem.h](../../Engine/SceneRuntime/AnimatorSystem.h)
- [Engine/SceneRuntime/SceneManager.cpp](../../Engine/SceneRuntime/SceneManager.cpp)
- [Engine/SceneRuntime/AnimationState.h](../../Engine/SceneRuntime/AnimationState.h)
- [ScriptCore/Animator.cs](../../ScriptCore/Animator.cs)
- [Editor/RenderTests/Animation/AnimationPlaybackSelfTest.cpp](../../Editor/RenderTests/Animation/AnimationPlaybackSelfTest.cpp)
