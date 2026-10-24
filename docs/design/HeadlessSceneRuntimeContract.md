# 렌더 없는 SceneRuntime 계약

2026-10-24 · 기존 Scene/Entity 재사용. [네트워크 계획](../plans/NetworkFrameworkPlan.md) · [수용 기록](../analysis/HeadlessSceneRuntimeAcceptance20261024.md).

## 실행 구조

`HeadlessHost`는 창과 렌더 장치를 생성하지 않는 콘솔 host다. `Runtime::Headless::Session`은 실행 순서만 조정한다. 엔티티 배열·Transform 계산·물리 바인딩·스크립트 수명을 따로 구현하지 않는다. `SceneRuntimeHeadless`는 기존 production 소스를 표현 서비스 없이 컴파일하는 target이다.

| 책임 | 기존 구현 |
|---|---|
| 씬 소유권·활성화·DDOL·종료 | SceneManager의 GC domain과 Scene root |
| 엔티티·컴포넌트·계층·Transform | Scene, Entity, Component, HierarchyStore, TransformStore |
| 수명·활성 상태·핸들 | LifecycleRegistry, ScriptComponent, ScriptObjectRegistry |
| 고정틱·명령·snapshot | SceneManager::Physics, SimulationTick, SimulationExchange, ExecuteSimulationTick |
| 바디·CCT·접촉 | Scene::FixedUpdate, ScenePhysicsSimulation, 실제 PhysX CPU backend |
| F# 로딩·필드·일괄 호출 | 기존 ClrHost, ScriptCore API45, 공통 F# ManagedRuntime |
| 관찰자 | 실제 Scene의 SceneObservation 엔티티/쓰기 통지 |
| cooked 입장 | Cedo::Decode → CookedSceneLoader → 실제 Entity/Component factory |

별도 `Runtime::Headless::Scene`/`Entity` 저장소와 HeadlessBindings 전용 native/managed ABI는 제거한다. 기존 Editor/Player 호출은 같은 Scene/Entity와 ClrHost API를 계속 사용한다.

`명령 적용 → F# OnTickSimulate(1/60) → 물리 단일 스텝 → 접촉/관찰 통지 → snapshot 준비 → 성공 커밋` 순서를 유지한다. 0틱 프레임에는 simulation·물리 호출이 없고 catch-up 상한은 4틱이다. 배치 전체가 실패한 틱의 snapshot은 게시하지 않는다. 개별 F# 콜백 예외는 기존 계약대로 해당 스크립트를 비활성화하고 나머지 배치를 계속 실행한다. 이 경우 정상 완료한 틱의 물리·snapshot은 커밋하며, 검증 host는 관리 오류 기록을 보고 실행 결과를 실패로 반환한다. 프레임당 F# PrePhysics/PostPhysics 호출과 고정축 호출은 기존 역할을 유지한다.

## cooked 문서와 선택 서비스

입력은 기존 AssetCooker scene producer가 만든 CEDO v1이다. 기존 `.creator`의 엔티티·컴포넌트 필드와 `m_Entities`/`m_SceneObjects` 읽기 별칭을 사용하며, 별도 `ce_headless_profile` 표식을 요구하지 않는다. 텍스트 폴백은 없다. sparse file slot·앞쪽에 아직 없는 부모 참조·UINT32_MAX 무효 참조와 레거시 `m_transform`도 실제 계층·Transform에 복원한다.

기본 reader는 Transform·PhysicsBodyComponent·CharacterMovementComponent·ScriptComponent를 실제 네이티브 컴포넌트에 복원한다. shape 구성과 CCT 검증·물리 등록은 기존 컴포넌트와 Scene이 수행한다. F# 필드도 기존 ScriptComponent의 복원 경로를 사용한다. 엔티티와 컴포넌트 identity, prefab/override 및 저작 메타데이터를 보존한다.

추가 CPU 컴포넌트는 `CookedSceneLoader::RegisterComponentReader`로 등록할 수 있다. reader는 기존 lifecycle 등록과 Entity의 컴포넌트 factory를 사용하고, 해당 Entity에 부착한 실제 Component를 반환해야 한다. 엔진의 별도 엔티티 형상이나 고정된 프로필 목록을 추가하지 않는다.

렌더·UI·애니메이션·오디오 및 외부 에셋을 사용하는 기능은 해당 서비스/provider가 필요하다. 현재 host가 설치하지 않은 component reader·asset bundle·collision geometry provider를 필요로 하는 콘텐츠는 입장 오류로 보고한다. 컴포넌트를 조용히 버리거나 성공한 것처럼 처리하지 않는다. 모든 기존 게임 씬의 무수정 실행을 수용했다고 확대하지 않는다.

공통 헤더에는 선택 서비스의 타입 선언이 남아 있을 수 있다. host adapter의 직접 서비스 의존과 project references를 검사하고, 최종 import·링크 map·패키지·로드 모듈에서 실제 렌더/Editor/저작 parser 의존을 차단한다. 소스 include 선언과 최종 링크 코드는 구분한다.

## 수명과 소유권

SceneManager가 같은 owner thread의 GCCE domain에서 Scene·Entity·Component를 소유한다. 기존 pending lifecycle 큐가 유일한 네이티브 수명 드라이버다. 처음부터 비활성인 스크립트는 기존 Scene 규칙대로 초기화를 지연한다. 별도 host 수명 큐로 먼저 초기화하지 않는다.

파괴 훅이 끝날 때까지 ScriptObjectRegistry 핸들을 유지하고, 그 뒤 기존 Scene의 지연 파괴·등록 해제·graph retirement·수집을 수행한다. body 제거 뒤 오래된 wrapper는 stale 오류를 반환한다. DDOL은 기존 SceneManager의 detach/attach를 사용한다. Entity·ScriptComponent·관리 인스턴스와 전역 script handle을 유지하고, 씬 범위 EntityHandle·SDK body handle·명령 epoch는 새 씬 경계에 맞게 갱신한다.

종료는 기존 Play 종료와 SceneManager::Decommissioning을 사용한다. 정상·실패 경로 모두 소유자를 정리하고, 관리 예외가 기록된 실행은 성공으로 보고하지 않는다. CoreCLR 자체의 수명은 프로세스 단위다.

Entity의 비직렬화 prefab snapshot과 Editor Play snapshot은 기존 호출자가 계속 사용한다. 빈 `Authoring::Document`의 소유권 구현을 backend 접근 구현에서 분리하며, 각 Adopt/Clone은 독립된 writer를 소유한다. 중립 utility와 저작 구현을 같은 Unity object로 합쳐 parser가 링크되는 것도 차단한다.

## 빌드·패키지·검증

Windows x64, MSVC v145/C++23, .NET 10 SDK/runtime, 기존 vcpkg dependency와 F# 도구 저장소를 사용한다. Linux·컨테이너·self-contained 배포는 별도 수용이다.

```powershell
./Tools/regression/verify-headless-scene.ps1 -Configuration Release
./Tools/regression/verify-headless-scene.ps1 -Configuration Debug
```

검증기는 실제 native target·관리 fixture를 빌드하고 AssetCooker로 씬을 굽는다. `stage-headless.ps1`은 EXE·nethost·GCCE·fmt·CPU PhysX DLL·관리 어셈블리·cooked 씬을 명시적으로 배치하고 SHA256 원장을 만든다. dependency bin 전체를 복사하지 않는다.

30/60/144 cadence의 고정틱·바디·CCT 궤적과 실제 시계 실행, 기존 F# lifecycle·명령/snapshot·stale wrapper, 실제 SceneManager DDOL, malformed 입력·미설치 서비스·틱/종료 예외를 검사한다. 종료 후 GC 객체·루트·스크립트 핸들 0과 물리 resource balance도 검사한다. cadence 입력은 합성이며 FPS 성능 측정으로 취급하지 않는다.

별도 checkout에는 명시한 source overlay만 적용하고 native/managed 산출물을 다시 빌드한다. 외부 dependency/NuGet cache 재사용과 F# 도구 revision을 기록한다. NetworkHandoff 기본 suite의 HeadlessBoundary와 Product gate를 유지한다.

## 서버 담당자에게 남는 범위

N0 → N2/N4 → N5 → N6 → N7 → N10 착수 순서를 유지한다. 이 host의 로컬 handle·command/schema 1을 wire identity/serializer로 사용하지 않는다. 실제 N9 서버 bootstrap·2-client 동기화·connection drain·운영 배포는 별도 제품 수용이다.
