# SceneRuntime 저작·수명 관찰 계약

2026-10-22 · W4 → ND-b · 로컬 어댑터 확장점

## 소유와 구독

`SceneManagers->Observations()`가 `Runtime::ISceneEntityObserver`와 `Runtime::IComponentWriteObserver`를 받는다. SceneObservation.h는 렌더·Editor·저작 parser를 포함하지 않는 C++20 값/인터페이스 헤더다. ND-a의 동결 wire 헤더 6종과 별개이며 wire ABI를 변경하지 않는다.

구독은 shared_ptr로 콜백 소유권을 보유하며 정수 토큰으로 해제한다. 같은 인터페이스 인스턴스 중복 구독은 같은 토큰을 반환한다. 슬롯 16개가 차거나 토큰 공간이 소진되면 0을 반환한다. 어댑터는 토큰을 보관하고 자신이 소유한 토큰만 해제한다. Play/Stop·씬 교체에서는 구독을 보존하고, SceneManager 종료 시 모든 엔티티의 retirement 뒤 Clear한다. Clear 이후 이전 토큰은 재사용하지 않는다.

구독/해제/Drain은 SceneManager 소유 스레드에서 수행한다. 연결 시 해당 스레드에서 Drain 후 구독하고 GetScenes/Scene의 현재 엔티티를 열거해 초기 상태를 구성한다. 과거 이벤트는 재생하지 않는다. 콜백은 값 기록·dirty 누적만 수행하며 Scene/엔티티 구조 변경을 재진입 호출하지 않는다. 변경 요청은 엔진의 다음 안전 경계로 전달한다.

## 엔티티 수명

이벤트는 `instance`, 이전/다음 `{scene,index,generation}`, Created/Detached/Attached/Destroyed를 복사한다. Entity*를 보유하지 않는다. 이 숫자는 프로세스 로컬 정체성이며 네트워크 ID/권한/schema로 전송하지 않는다. N2의 NetEntityId 매핑은 네트워크 어댑터 소유다.

Created는 슬롯 예약이 아닌 실제 생성/로드/트리 편입 완료에서 발행한다. 미완료 슬롯 rollback에는 생성 이벤트가 없다. Destroyed/Detached는 슬롯·선택·세대 정리가 끝난 뒤 발행한다. DDOL은 같은 instance의 Detached → Attached이며 Attached는 목적 씬 전체 계층 root remap 뒤 발행한다. 원 EntityHandle은 무효가 되지만 ScriptObjectHandle은 실제 파괴 시까지 유지한다. 씬 root의 retirement 및 분리 상태에서 직접 finalization되는 실패 정리도 terminal 이벤트를 한 번 발행한다.

## 일반 쓰기와 W4

`NotifyPropertyChanged(name, source)`가 기존 가장 파생된 OnPropertyChanged를 호출하고 일반 쓰기 이벤트를 발행한다. 따라서 base hook을 부르지 않는 Transform/RectTransform도 관측된다. setter가 해당 hook 안에서 다시 발행하는 중복은 동일 컴포넌트 범위에서 억제한다. 렌더 전용 PublishRenderProxyDirty는 일반 쓰기 신호로 사용하지 않는다.

Inspector scalar/custom drawer는 중앙 CommitProperty를 사용한다. enum/container/nested 값은 최상위 컴포넌트 필드의 단일 transaction으로 수집한다. 중첩 위젯의 임시 주소를 Undo closure에 남기지 않는다. Undo는 EntityReference와 컴포넌트 instance ID로 재탐색하므로 삭제/씬 교체 후 쓰지 않는다. 실제 변경한 필드만 통지하며 no-op 편집은 Undo/통지를 만들지 않는다. Inspector/Undo/Redo 출처를 구분한다. 명시적 저작 replay에서는 null로 저장된 빈 값 컨테이너를 비운다. 일반 파일 로더의 null-means-preserve 규칙과 포인터 원소 시퀀스의 건너뛰기 규칙은 보존한다. 전체 authoring 문서를 유지해 OnDeserialized의 기존 복원 입력을 보존하고, 통지 필터만 변경 필드로 제한한다.

비렌더 컴포넌트의 reflection/Prefab도 같은 입구를 사용한다. native render 값 setter는 렌더 dirty와 일반 쓰기를 함께 발행한다. Component.Enabled·물리 shape의 검증된 SetShapes/ReplaceShapes 및 Transform의 accepted 단일/배치 writer(physics packed writer 포함)를 연결한다. Transform worker는 graph가 고정되고 job이 join되는 기존 쓰기 구간에서 값 이벤트만 enqueue한다. raw public C++ 필드 대입을 자동으로 가로채지는 않는다. 새 writer는 NotifyPropertyChanged 또는 PublishPropertyWrite를 호출해야 한다.

쓰기 이벤트는 entity/component instance, owner location, 출처 및 최대 127바이트 필드 이름을 복사한다. 전체 갱신 또는 더 긴 이름은 allProperties=true다. 어댑터는 정확한 이름만 가정하지 말고 전체 재검사 신호를 처리한다. 필드별 관측은 변경 값 payload를 제공하지 않으며 어댑터가 안전한 스냅샷 경계에서 읽는다.

## 배달과 실패

작업 스레드는 256개 copied-event 큐에 넣고 owner에서만 콜백을 실행한다. owner의 publication은 즉시 Drain하며 작업 스레드 큐는 고정 틱 snapshot capture 앞과 EndOfFrame에서 비운다. 콜백은 mutex 밖에서 호출하며 snapshot의 shared_ptr가 실행 중 수명을 보장한다. 콜백 도중 해제한 구독은 다음 호출부터 제외한다.

콜백 예외는 cleanup을 중단시키지 않고 failures를 증가시킨다. 큐 포화 및 무한 재진입 방지를 위한 Drain 256회 예산 초과는 lost로 계수한다. 어댑터는 Metrics의 lost/failures 변화 시 증분 상태를 신뢰하지 말고 전체 상태를 재구성해야 한다. 이 관찰자는 신뢰성 있는 transport나 무손실 replication journal이 아니다. 네트워크 송신은 성공 tick snapshot adapter와 결합해 구현한다.

## 검증 진입점

- `verify-scene-observation.ps1 -Configuration All`: 실제 헤더를 사용한 Debug/Release 구독·재진입·스레드·예외·포화 검사.
- `verify-scene-gc.ps1 -Configuration Release -Iterations 2`: 실제 Editor의 typed authoring commit/Undo/Redo, native/Transform/비렌더 쓰기, 엔티티 세대와 DDOL 통합 검사.
- `run-all.ps1 -Suite NetworkHandoff -Configuration All -Product -EditorExe <Release Editor> -CookedStage <새 cooked 패키지>`: ND-a·W4/ND-b·N1·N3·물리 ABI/identity·lifecycle 및 실제 N3 cadence/moving GPU 등록 회귀. W5 렌더 소비/GPU·실제 transport/서버 제품 gate는 별도다.
