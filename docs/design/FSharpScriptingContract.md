# F# 단독 저작·네이티브 컴포넌트 계약 — 2026-10-23

## 현재 결정

게임 스크립트와 공통 관리 실행기는 F#으로 구현한다. `ScriptRuntime/ScriptRuntime.fsproj`가 `CreatorEngine.ManagedRuntime.dll`을 만들며, 팩토리·인스턴스/틱 멤버십·생명주기 실행·구독 정리·await 재개 큐·접촉 라우팅·collectible 로더·애니메이션/BT 등록과 인스턴스·진단 호출·GC 정책을 소유한다. C# ScriptCore에는 네이티브 함수 포인터·ABI 값 형식·공개 바인딩 타입과 전달 진입점을 남긴다. C# 바인딩에는 별도의 스크립트 실행 목록이나 생명주기 실행기를 두지 않는다.

컴포넌트 생명주기 11종은 `override` 없이 일반 인스턴스 멤버로 선언한다. 생성기가 이름과 시그니처를 검사해 `RegisterAllLifecycle`에 형식이 확정된 콜백을 등록한다. Component 기반 타입에는 가상 생명주기를 두지 않는다. `member this.OnTickSimulate(dt: single) = ...`가 동기 고정틱 훅이며, PrePhysics/PostPhysics의 인자도 single, 나머지 훅은 무인자이고 모두 unit을 반환한다. 외부 생성 파일에서 접근할 수 있는 public/internal 멤버만 지원한다. private·static·generic·curried·overloaded·잘못된 인자/반환형은 CEF1020 오류로 거절한다. 생명주기는 일반 메시지 표에서 제외한다.

11종은 지원 목록이다. **필요한 훅만 작성하며, 생명주기가 하나도 없는 컴포넌트도 유효하다.** 새 Editor 템플릿은 타입과 `inherit Component()`만 만든다. 빈 `OnTickSimulate`를 기본 생성하지 않는다. 동작·정리·순서 검증을 수행하는 기존 훅은 유지하고, 비어 있는 훅만 제거한다.

기반 virtual의 추론이 없어졌으므로 쓰지 않는 틱 인자는 `(_: single)`로 지정한다. 예외만 던지는 훅도 `member _.OnInitialized() : unit = invalidOp "..."`처럼 반환형을 명시한다. 그렇지 않으면 F#이 generic 인자/반환형으로 추론할 수 있다. 이주한 제품 fixture에는 single/unit 서명을 명시했다.

C++ ClrHost는 기존 unmanaged `Bootstrap.OnTickSimulate(float dt)`로 틱당 한 번 진입하며, 이후 순회·콜백 호출은 F# 실행기가 담당한다. 없는 훅은 null 콜백으로 표현해 빈 기본 함수를 호출하지 않는다. 매 틱 reflection이나 문자열 기반 생명주기 검색은 없다. API45는 네이티브 바인딩과 F# 공통 런타임에서 함께 검사하며, 새 로더는 생명주기 등록 표가 없는 스크립트 DLL을 거절한다. 이전 OnUpdate 이름은 제거한다. 60Hz/최대 catch-up4, 명령 적용→관리 tick→물리→접촉→성공 snapshot 순서, Enabled/세대/스레드/예외 격리와 기존 frame Pre/PostPhysics 의미는 유지한다. OnSimulate·Scope.Delay·F# 전용 simulation CE를 복원하지 않는다.

```fsharp
type Mover() =
    inherit Component()

    member this.OnTickSimulate(dt: single) =
        this.Transform.Translate(Float3(dt, 0.0f, 0.0f))
```

## 생성·저작·배포

- [도구 저장소](https://github.com/29thnight/FSharp_Tool_Pakage_for_CreatorEngine)의 FCS 분석·파생 wrapper·정적 registry/field accessor/message switch를 사용한다. F# 타입의 lifecycle은 메시지 dispatch에서 제외한다.
- GameScripts/GameScripts.fsproj가 현재 Editor 빌드의 저작 프로젝트다. 별도 GameScripts.csproj는 기존 내부 C# 회귀에 남으며 F# 프로젝트가 참조하지 않는다.
- Assets/Script/compile.order는 의존 순서대로 상대 .fs 경로를 한 줄에 하나 기록한다. Editor 생성은 새 파일을 마지막에 추가한다. 기존 순서를 알파벳으로 재정렬하지 않는다.
- SDK에는 공통 실행기 CreatorEngine.ManagedRuntime.dll·deps와 엔진용 FSharp.Core.dll을 Managed 루트에 고정 배포한다. ScriptRuntime 프로젝트는 CopyLocalLockFileAssemblies로 실제 의존성을 배치한다. Scripts 아래에는 게임용 FSharp.Core와 조회 편의용 CreatorEngine.FSharp.Runtime.dll을 따로 배포한다. 이 helper DLL과 엔진 공통 실행기는 별개다. compiler private FSharp.Core도 별도 경로로 보존한다. 고정 CoreFiles/ManagedFiles 목록과 패키지 hash로 누락·변경을 검사한다.
- F# GameScripts.dll이 유일한 primary다. 엔진 공통 실행기와 엔진용 FSharp.Core는 ScriptCore가 올라간 동일한 상주 컨텍스트에 둔다. hostfxr의 component 컨텍스트를 Default로 가정하지 않는다. 스크립트의 collectible 컨텍스트에는 실제 ScriptCore를 공유하고, 게임용 FSharp.Core/private dependency는 그 세대 안에 로드한다. DLL/PDB stream load, 실패한 사전 검증의 이전 코드/인스턴스 보존, 성공한 reload의 콜백·팩토리·인스턴스·재개 큐 참조 해제를 유지한다. 공통 엔진 런타임의 코드 교체는 프로세스 재시작을 요구한다.
- 스크립트별 C# 파일을 생성하지 않는다. ScriptRegistry.g.fs에 F# 필드/메시지 접근자·factory와 별도의 정적 생명주기 표를 만든다. factory가 만든 객체 하나에 타입별 콜백 표를 연결하며, 콜백은 저작 타입의 일반 멤버를 직접 호출한다. 접근자의 virtual 재정의는 인스펙터/직렬화용 내부 구현이다. GameScripts의 대응 .cs는 이주 전 원본/내부 비교 입력이며 F# 제품 프로젝트에 포함하지 않는다.
- public mutable val + SerializeField의 기존 지원 값 형식만 저장한다. DU/record/option의 임의 저장·AOT·IDE breakpoint 지원은 별도다.

## 사람이 읽는 저작 코드와 생성 코드

[VS Code 작성 도우미](../../../FSharp_Tool_Pakage_for_CreatorEngine/vscode/README.md)는 현재 컴포넌트의 멤버 위치에서 `On…` 자동완성과 **생명주기 추가** 선택 메뉴를 제공한다. 선택한 일반 멤버만 single/unit 서명으로 삽입하며 기존 훅은 제외한다. 주석·문자열·메서드 본문에는 제안하지 않는다. 파일 저장·빌드·compile.order 변경은 수행하지 않는다. 다른 파일의 기반 타입과 복잡한 F# 구문은 편집 도우미가 추측하지 않으며 최종 검사는 FCS 생성기가 담당한다. [선택적 저작 수용 기록](../analysis/FSharpOptionalLifecycleAcceptance20261023.md)을 따른다.

F# 스크립트와 내부 C# 바인딩은 사람이 수정·검토하는 코드다. 공백만 늘리는 대신 다음 기준을 적용한다.

- 상태와 단계는 역할을 드러내는 이름을 사용한다. 여러 의미를 겸하는 정수 단계 대신 명명된 상태와 별도 처리 메서드를 둔다.
- 생명주기 본문은 실행 순서를 보여 주고, 명령 접수·스냅샷 검증·정리 등 독립된 책임은 이름 있는 메서드로 나눈다. 핫 틱에 새 할당이나 reflection을 넣지 않는다.
- ABI 필드는 선언 순서를 유지하면서 한 줄에 하나씩 쓴다. 포인터·출력 인자·실패 조건을 명시하고, 긴 생성자 대입을 튜플 한 줄로 압축하지 않는다.
- 생성 접근자의 이름에 원본 스크립트 이름을 포함하고, factory와 접근자 구획을 구분한다. 생성기에서 고쳐 재생성 후에도 유지한다.
- 주석은 상태의 수명, 호출 순서의 이유, ABI 제약을 설명한다. 코드의 단순 반복 설명은 추가하지 않는다.

AniBehavior와 BT 노드도 F#의 정적 factory 등록을 사용한다. 컴포넌트·애니메이션·BT 카탈로그는 기존 호스트 계약대로 구분하며, 매 틱 reflection으로 생성하지 않는다.

## 네이티브 컴포넌트와 성능

F# → 기존 ScriptCore concrete wrapper → C ABI table → C++ component로 접근한다. Transform/Camera/Light/Animator/PhysicsBody/CCT에 별도 mirror 객체·틱 reflection·F# 수명 관리자를 추가하지 않는다. wrapper가 없는 native component의 자동 공개는 하지 않는다.

NativeComponents.tryGet<T>는 기존 GetComponent<T>를 1회 호출하고 struct voption을 반환한다. 기존 조회는 wrapper를 할당하므로 초기화/필요한 재편입 시 조회하여 cache한다. Transform 등의 entity generation 검사와 PhysicsBody/CCT의 NativeInstance 검사는 서로 다른 계약이다. 제거된 body/character wrapper를 새 component에 재사용하지 않는다.

API45 관리 테스트의 100,000회 반복에서 조회 direct/helper 각각9.6MB, cache LocalPosition 읽기는 반복 구간 추가 할당0 bytes였다. helper는 기존 wrapper 비용을 없애지 않는다. 단일 타이밍에서 helper가 더 느린 표본도 있어 CPU 성능 동등/향상으로 판정하지 않는다. cache 읽기는 iteration당 callback1회·추가 Exists 조회0회였다. 제어 native callback 관리 호스트 결과이며 실제 프레임 비용은 아니다. 이전 API44 기록의12.8MB는 당시 구현의 측정값이다.

hot tick에서는 seq/list/closure 생성, 반복 GetComponent, 작은 native 호출을 숨기는 collection pipeline을 피한다. Span은 동기 본문에서 처리하고 외부 async 결과는 기존 명시 tick 명령으로 전달한다. 새 수명·시간 효과 계층은 없다.

## 수용 경계

[API45 실행 기록](../analysis/FSharpManagedRuntimeAcceptance20261023.md)을 따른다. API43/OnUpdate와 API44/override 기록은 이전 단계의 수용이며 API45의 코드 신원 증명이 아니다. 새 source identity·관리 gate·실제 제품 gate를 구분한다. 전체 기존 fixture 이주·shader/일반 모델 cook·IDE·NativeAOT·다른 플랫폼·장시간 reload·일반 성능 예산은 자동 완료하지 않는다.
