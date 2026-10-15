# ScriptCore 공개 표면과 네이티브 연결

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 사용자 코드의 출발점

사용자 C# 스크립트는 `CreatorEngine.Component`를 상속한다. 현재 훅은 OnInitialized, OnAddedToScene, OnBeginSimulation, OnEndSimulation, OnRemovingFromScene, OnUninitializing이며 활성 축의 OnEnable/OnDisable과 프레임 틱 PrePhysics/PostPhysics는 별도다. 옛 Behaviour·Awake·Start·Update·LateUpdate를 현재 public contract로 소개하지 않는다.

ScriptCore는 net10.0, unsafe function pointer ABI, compile-time ScriptCore.Generators analyzer를 사용한다. ClrHost가 hostfxr/CoreCLR를 열고 Bootstrap에 native table을 전달한다. 관리 entrypoint와 native function table은 서로 반대 방향이며 version/struct size/layout을 일치시켜야 한다. table에 field를 추가할 때 C++·C#·버전 생성·체크 도구를 함께 변경한다.

## 객체와 컴포넌트

Entity는 ObjectHandle을 담은 비소유 값이다. IsAlive는 핸들 형식과 native registry resolve를 확인한다. GetComponent<T>는 C# script이면 ScriptRegistry, native wrapper이면 NativeComponentTable의 exists/wrap 경로를 사용한다. 없는 컴포넌트는 null이다. 이름이나 Index는 장기 정체성 대신 사용하지 않는다.

대부분 native wrapper는 owner handle로 매번 resolve한다. PhysicsBodyComponent/CharacterMovementComponent는 native instance ID도 캡처하므로 같은 Entity에서 컴포넌트가 교체된 뒤 옛 wrapper가 새 대상에 붙지 않게 한다. 모든 wrapper에 동일한 수명 검사가 있다고 일반화하지 않는다.

Transform이 없는 Entity도 있다. Component.Transform은 바인딩되지 않은 경우 예외이며 HasTransform 또는 nullable Entity.Transform/GetComponent로 분기한다. native Canvas 생성 규칙과 wrapper의 존재 검사를 확인해야 하며 오래된 주석만 보고 모든 Canvas에 Transform이 없다고 단정하지 않는다.

## lifecycle과 프레임 순서

native Scene이 단계 전이의 driver이며 ScriptComponent가 ScriptLifecyclePhase 값으로 관리 DispatchLifecycle에 전달한다. management _pendingAdd/_pendingRemove는 tick membership 큐다. 단계 성공 flag와 재진입 방지 flag가 분리돼 있어 예외로 실패한 opening 단계에 성공한 closing 단계를 임의로 짝짓지 않는다.

RuntimeFrame은 initialization/input→PrePhysics→physics→native GameLogic→post event flush→PostPhysics 순이다. 물리 내부는 60Hz catch-up이지만 C# 두 틱은 frame당 한 번이다. post 경계는 registration, physics, animation, named message, AI ticks를 flush한 뒤 PostPhysics를 호출한다. pending Scene structure change 동안 관리 앞/뒤 틱은 모두 생략한다.

## OnSimulate·Scope·예외

OnBeginSimulation 성공 뒤 OnSimulate Task가 한 번 시작된다. Scope.Delay는 engine dt로 진행하고 생성 프레임에는 시간을 소비하지 않으며 최소 다음 프레임까지 기다린다. NaN은 ArgumentException, 0 이하 값은 다음 프레임, positive infinity는 scope cancellation까지 대기한다.

PrePhysicsTick은 membership flush→모든 live scope Tick→GameThreadSynchronizationContext Drain→enabled PrePhysics 순이다. disabled script의 scope time은 계속 진행한다. Scope.Cancel은 cleanup 예외를 기록하면서 나머지 정리를 계속하고 pending delay와 cancellation registration을 해제한다. 정상 종료 순서는 scope 취소 후 OnEndSimulation이다.

일반 await는 설치된 context에 continuation을 게시하며 game-thread drain에서 재개한다. ConfigureAwait(false), 직접 background 접근, 긴 동기 루프는 안전을 자동 보장하지 않는다. 취소는 협조적이다. context.Send는 worker에서 거부하며 engine native 진입은 game-thread guard를 따른다. 훅/비동기 오류는 ScriptRegistry의 인스턴스 격리와 로그로 드러난다.

## 현재 지원 표면과 미러 한계

Transform, Input, Camera/CameraComponent, Animator, MeshRenderer, LightComponent, UIComponents, Audio/SoundComponent/AudioListenerComponent, Physics/PhysicsBodyComponent/CharacterMovementComponent, Prefab, Assets, BlackBoard/BehaviorTree가 현재 소스에 있다. native 타입이 있다고 C# wrapper가 자동 생성되는 구조는 아니다.

Input.GetKey는 Down와 Pressed를 모두 포함한다. gamepad trigger는 현재 boolean이며 아날로그 trigger 값을 제공하는 척하지 않는다. action callback의 이름 전달과 풍부한 typed action value 통로는 별도다. native property write마다 authoring dirty/undo 또는 render invalidation까지 동일하게 보장되는 것도 아니다. 각 binding이 실제 setter/publication을 호출하는지 확인해야 한다.

named callback은 public 무인자 void 메서드에 대한 generator의 InvokeMessage dispatch다. 임의 method reflection/RPC가 아니다. SerializeField, script 등록, EngineCallable 생성과 reload는 generator·loader 코드를 함께 봐야 한다. trim analyzer가 켜져 있어도 현재 제품이 NativeAOT로 출하된다는 뜻은 아니다.

## 남은 확장과 검증

풍부한 action value ABI, setter dirty/렌더 관측 하네스, native wrapper coverage 검증, override-only tick dispatch는 기존 의도다. 현재 ScriptRegistry는 active 목록을 순회해 PrePhysics/PostPhysics를 호출하므로 “오버라이드만 선택해 호출한다”는 완료 주장은 맞지 않는다. future fixed OnSimulationTick과 PrSM effect executor도 별도 계획이다.

ABI table·entrypoint·thread guard·lifecycle enum 검사 스크립트와 실제 lifecycle/reload fixture를 함께 사용한다. 소스 일치, compile, Editor Play, cooked Player, hot reload는 다른 단계이며 이번에는 소스만 대조했다.

## 구현 근거

- [ScriptCore/Component.cs](../../ScriptCore/Component.cs)
- [ScriptCore/Entity.cs](../../ScriptCore/Entity.cs)
- [ScriptCore/NativeComponent.cs](../../ScriptCore/NativeComponent.cs)
- [ScriptCore/Native.cs](../../ScriptCore/Native.cs)
- [ScriptCore/ScriptRegistry.cs](../../ScriptCore/ScriptRegistry.cs)
- [ScriptCore/SimulationScope.cs](../../ScriptCore/SimulationScope.cs)
- [ScriptCore/GameThreadSynchronizationContext.cs](../../ScriptCore/GameThreadSynchronizationContext.cs)
- [ScriptCore/Input.cs](../../ScriptCore/Input.cs)
- [ScriptCore/ScriptCore.csproj](../../ScriptCore/ScriptCore.csproj)
- [ScriptCore.Generators/ScriptGenerator.cs](../../ScriptCore.Generators/ScriptGenerator.cs)
- [Engine/SceneRuntime/ClrHost.cpp](../../Engine/SceneRuntime/ClrHost.cpp)
- [Engine/SceneRuntime/RuntimeFrame.cpp](../../Engine/SceneRuntime/RuntimeFrame.cpp)
