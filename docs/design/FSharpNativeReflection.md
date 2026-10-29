# F# 네이티브 컴포넌트와 값 그래프 바인딩

## 목적과 진입점

API 48에서 추가한 `NativeReflection_Execute`, `NativeReflection_Read`,
`NativeReflection_Release` 세 슬롯으로 F# `CreatorEngine.NativeBindings`가
네이티브 반사 서비스를 직접 호출한다. API 49는 등록된 콜백의 세대·수명을
관리하는 다섯 슬롯을 테이블 끝에 추가한다.
`CreatorEngine.NativeTypes`에는 등록된 네이티브 컴포넌트 27개의 개별 F# 구현과
각 멤버에서 사용하는 레코드·컬렉션·반환 타입의 뷰가 있다. 공통
`NativeObject` / `NativeValue` 경로는 동적 타입 탐색과 도구 작성에도 사용한다.

컴포넌트 정본은 `Meta::Types()`에 등록된 `Component` 파생 타입이다.
생성 메뉴에 보일 타입만 고르는 `ComponentFactory` 필터를 카탈로그로 쓰지
않는다. 카탈로그는 상속, 필드, 메서드 인자와 결과, 컨테이너 원소까지
재귀적으로 순회한다. 재귀 타입은 타입 ID로 한 번만 방문한다.

```fsharp
open CreatorEngine

// entity는 현재 스크립트가 가진 Entity다.
for component in entity.NativeComponents do
    printfn "%s / %d" component.TypeName component.InstanceId
    for field in component.Describe().Fields do
        printfn "  %s: %s (write=%b)" field.Name field.TypeName field.CanWrite

let light = entity.GetNativeComponent("LightComponent")
light.Invoke("SetIntensity", [| box 2.0f |]) |> ignore
let intensity = light["m_intencity"].Read<float32>()
```

기존 전용 래퍼에는 `.Reflected` 확장이 있다. 이것은 현재 소유자에서 해당
컴포넌트를 다시 조회한다. 반환된 `NativeObject`는 조회 시점의 인스턴스 ID를
기억하므로 이후 컴포넌트를 제거하고 같은 타입을 다시 추가해도 이전 객체로
새 컴포넌트를 수정할 수 없다.

## 컴포넌트별 타입이 있는 F# 구현

`ScriptNative/GeneratedNative/Components`의 `*View.fs`는 각각 한 네이티브
컴포넌트의 필드와 메서드를 구현한다. 기존 래퍼의 `.Native`로 연결하거나
생성된 `*View.From(entity)`로 직접 조회할 수 있다. 두 경로 모두 조회한
컴포넌트의 인스턴스 ID를 보존한다.

```fsharp
open CreatorEngine
open CreatorEngine.NativeTypes

let light = entity.GetComponent<LightComponent>().Native
light.SetIntensity(2.0f)
light.SetColor(math_colorView.Create(1.0f, 0.6f, 0.2f, 1.0f))

let profile = SceneRenderProfileComponentView.From(entity).GetRenderProfile()
profile.settings.bloom.threshold <- 3.0f

let terrain = TerrainComponentView.From(entity)
let material = terrain.GetMaterialShared().Value
let constants = material.m_layerBufferData

let machine = StateMachineComponentView.From(entity)
let state = machine.FindStateByName("Idle")
if state.HasValue then
    printfn "%s" (state.Value.GetName())
```

기존 `Transform`, 카메라·라이트·물리·UI·오디오 래퍼의 전용 API는 그대로
사용할 수 있다. `ScriptComponent`와 `InvalidScriptComponent`도 F# 래퍼와
컴포넌트 조회 팩토리를 갖는다. 이전에 자체 멤버가 없던 행동 트리·폴리지·입력·
렌더 프로필·스프라이트·스프라이트 시트·상태 기계·지형 래퍼 역시 `.Native`에서
생성된 멤버를 사용한다.

생성 기준은 실제 RenderEngine/SceneRuntime 선언과 별도로 정의된
수학·Lattice·물리·렌더 핸들·씬 서비스 등의 런타임 스키마다.
`EnumTags.fs`의 열거형은 각 네이티브 타입을 구분하는
`NativeEnumValue<Tag>`로 표현한다. 이름이나 숫자는 해당 `*Values` 모듈에서
만들며, 유효성은 네이티브 직렬화 규약에 따라 검사한다.

`NativeArray<'T>`, `NativeMap<'Key, 'Value>`, `NativeReference<'T>`,
`NativeOptional<'T>`, `NativeExpected<'T, 'Error>`와 tuple/variant 뷰가 중첩
원소 타입을 유지한다. `.Value`, 인덱서, 필드 접근은 네이티브 경로를 이어 간다.
값 인자는 각 타입의 `Create(...)`, `With_<필드>(...)`, `FromJson(...)`으로
구성할 수 있다. 이 생성 함수는 클라이언트의 읽기 전용 값 스냅샷을 만들며,
네이티브 객체를 생성하거나 기존 객체를 수정하지 않는다. 지원 가능한 필드
구성에만 `Create`를 생성하고, 나머지 타입은 명시적 JSON 값 또는 기존 뷰를
사용한다. 네이티브 `x/y/z/w`와 관리 값의 `X/Y/Z/W`는 전용 codec으로 구분한다.
각 뷰의 `Codec`과 `NativeArray<'T>.FromArray(values, codec)` / `Empty(codec)`로
배열 인자도 F# 값에서 만들 수 있다. mutable 참조와 `span<T>` 출력 버퍼는
F# `byref` 인자로 생성하며, 호출 뒤의 네이티브 인자 배열을 새 값에 반영한다.
`span<const T>` 입력과 mutable 출력 버퍼는 서로 다른 오버로드로 유지한다.

`coverage.json`은 모든 추출된 필드·메서드의 F# 이름, 상태, 호출 방식,
반환 수명과 미지원 사유를 기록한다. 지원되지 않는 항목에는
`*_Unavailable` 설명을 제공한다. 생명주기 내부 훅, 임의 메모리 버퍼,
소유권 이전과 별도 수명 규약이 필요한 타입은 호출 가능으로 표시하지 않는다.
생성된 메서드도 실행 시 네이티브 서술자의 직렬화·호출 가능 여부를 확인한다.
생성 범위와 실제로 실행해 검증한 제품 메서드의 범위는 구분한다.

오버로드는 원래 C++ 이름·인자 타입·참조 전달 방식·const 여부로 현재 네이티브
카탈로그에서 하나를 고른 뒤 정확한 런타임 서명을 사용한다. C++ 선언 문자열을
임의로 런타임 서명으로 만들지 않는다. 성공한 조회만 현재 바인딩 인스턴스에
캐시하며, 반환 경로를 미리 실행해 getter를 중복 호출하지 않는다.

## 중첩 값, 배열, 맵, 반환값

```fsharp
let profile =
    entity.GetNativeComponent("SceneRenderProfileComponent")
          .Borrow("GetRenderProfile")

profile["settings"]["bloom"]["threshold"].Write(3.0f)
let threshold = profile["settings"]["bloom"]["threshold"].Read<float32>()

let terrain = entity.GetNativeComponent("TerrainComponent")
let material = terrain.Borrow("GetMaterialShared").Dereference()
let layerConstants = material["m_layerBufferData"].ReadJson()
```

`Field(name)` 또는 문자열 인덱서는 필드를, `At(index)` 또는 정수 인덱서는
컨테이너 원소를 선택한다. `Key(key)`는 맵 키를 찾는다. `Dereference()`는
optional이나 지원되는 포인터·소유자 래퍼의 현재 대상을 선택한다.
`Describe().Count`로 컨테이너의 현재 원소 수를 확인할 수 있다.

`Borrow(name)`은 인자 없는 getter를 경로에 추가한다. 요청할 때마다 getter를
다시 호출하며 네이티브 주소를 관리 코드에 보관하지 않는다. 참조 반환값은
원래 대상을, 소유권을 가진 반환값은 요청 동안 유지되는 대상을 가리킨다.
값으로 복사된 레코드의 반환값은 읽기 전용이다. 복사본 수정이 엔진 상태를
수정한 것처럼 보이게 하지 않는다.

`Borrow(name, arguments)`와 인자 배열을 받는 `BorrowOverload`는 안전한 값
인자를 받는 const 조회 메서드도 지원한다. 전체 인자 배열을 검증한 뒤 호출하며,
반환값과 하위 뷰가 사용하는 동안 문자열·span 등의 인자 저장소를 유지한다.
인자가 있는 조회의 결과는 임시 인자를 참조할 수 있으므로 항상 읽기 전용이다.
mutable/out 참조, rvalue 참조, mutable span, 주소·callback 입력은 이 경로에서
거절한다. `AddState`처럼 객체를 생성하는 비const 메서드의 포인터 반환값을
재실행되는 getter 경로로 가장하지 않는다.

오버로드는 서술자의 정확한 `Signature`를 사용한다.

```fsharp
let methodInfo =
    terrain.Describe().Methods
    |> Array.find (fun m -> m.Name = "GetMaterialShared" && m.CanBorrowResult && m.IsConst)
let value = terrain.BorrowOverload(methodInfo.Name, methodInfo.Signature).Dereference()
```

`InvokeOverload`도 같은 서명을 받는다. `Invoke` 결과의 `ReturnValue`는 JSON,
`Read<'T>()`는 관리 값으로의 역직렬화, `Arguments`는 호출 이후의 인자 배열이다.
마지막 항목에는 수정된 native reference 인자도 포함된다. 레코드의 JSON 필드
이름은 필드 서술자의 `SerializedName`을 사용한다. 경로의 `Field(name)`과 문자열
인덱서는 원래 네이티브 필드 이름인 `Name`을 사용한다.

## 기존 네이티브 객체를 받는 호출

`InvokeBound`는 JSON으로 만들 수 있는 값과 이미 존재하는 네이티브 객체를
하나의 호출에 섞어 전달한다. 생성기는 포인터·소유자·내부 포인터가 있는
레코드 인자를 받으면 이 경로를 선택한다. F# 뷰는 주소 대신 소유자 generation,
컴포넌트 instance와 필드·컨테이너 경로를 보낸다. 네이티브 쪽은 모든 인자의
정확한 타입, const 상태와 생존 여부를 검증한 뒤 메서드를 한 번 호출한다.
분리된 JSON 스냅샷을 기존 네이티브 객체처럼 넘길 수 없다.

인자 경로는 필드·원소·키·역참조와 호스트가 지원하는 소유 객체 ID로 구성한다.
인자 안의 getter 실행은 앞서 해석한 객체를 무효화할 수 있으므로 허용하지
않는다. 임의 조회 결과를 저장한 것처럼 사용하려면 아래 FSM 자식 객체와 같이
호스트가 수명을 확인하는 ID 경로가 필요하다. 콜백은 등록 토큰으로 별도 해석한다.

호출의 반환값과 JSON으로 생성한 mutable 참조 인자 출력은 요청 안에서
직렬화한다. 기존 네이티브 객체를 넘긴 인자의 출력 슬롯은 `null`이다.
메서드가 그 객체를 제거했을 수 있으므로 호출 뒤 다시 읽지 않는다.
기본 반환값은 읽기 전용 스냅샷이다. FSM 상태·전이의 포인터 반환에는 별도의
소유 객체 ID 경로를 사용하므로 다음 호출에서도 같은 객체를 사용할 수 있다.

### FSM 상태와 전이의 안정된 ID

`StateMachineComponent.AddState`, `AddTransition`, `FindStateByName`과
`Transition.GetFrom`·`GetTo`는 메서드를 한 번 실행하고 반환된 자식 객체의
ID를 캡처한다. 생성된 F# 뷰는 객체 주소나 상태 이름 대신 엔티티 generation,
컴포넌트 instance, 자식 타입과 ID를 보관한다. 같은 이름의 상태를 만들거나
상태 이름을 바꾸어도 기존 뷰는 원래 객체를 가리킨다.

```fsharp
let machine = StateMachineComponentView.From(entity)
let idle = machine.AddState("Idle")
idle.Value.SetName("Rest")
let walk = machine.AddState("Walk")
let condition =
    NativeCallbacks.Condition(this, fun board -> board.GetValueAsBool("isMoving"))
let transition = machine.AddTransition(idle, walk, condition)
machine.RemoveState(idle)
```

각 요청은 현재 컴포넌트에서 ID를 다시 확인하고 요청이 끝날 때까지 객체를
유지한다. 제거된 ID는 재사용하지 않으며, `RemoveState`는 그 상태에 연결된
전이 ID도 즉시 무효화한다. 위 예제의 `idle`과 `transition`은 제거 이후
`stale_object` 오류를 반환하고 `walk`는 계속 유효하다. 요청 중 객체를
유지하는 것과 제거된 객체를 다시 조회할 수 있는지는 별개의 규칙이다.
다른 상태 머신의 상태를 전이의 끝점으로 넘기거나 다른 타입의 ID를 사용하는
요청도 거절한다. 조회에 실패한 포인터의 null 상태는 그대로 표현한다.

서비스는 첫 경로 요소의 `owned` 선택자로 이 ID를 해석한다. `captureResult`
요청은 호스트의 정확한 반환 타입 지원 여부와 쓰기 가능 여부를 호출 전에
검사하며, 결과 투영이나 결과 버리기 옵션과 함께 사용할 수 없다. 엔진의
`StateMachineChildren` 저장소와 동일한 어댑터를 계약 fixture에서도 사용한다.

`reflgen::script_once`는 인자가 없는 소유권 소비 메서드도 재실행되는 getter
경로에서 제외한다. 이 표기만으로 엔진 내부의 소유권 이전을 스크립트에
허용하지는 않는다. 호출이 스크립트에 공개되어 있고 반환값을 유용한 데이터로
복사할 수 있을 때만 한 번 호출한 결과를 읽기 전용 스냅샷으로 제공한다.
계약 fixture의 `TakeShared`는 소유자를 한 번 소비하고 `Settings.count`를
스냅샷에서 읽으며, 결과 재조회와 거부된 `Borrow`가 메서드를 다시 실행하지
않는 것을 검사한다.
결과를 읽기 전에 쓰기 알림을 발행하면 알림이 반환 저장소를 무효화할 수 있어,
응답을 먼저 캡처하고 `NotifyPropertyChanged`를 호출한다.

`Scene.SetPreparedTextureImagePins`와 `TakePreparedTextureImagePins`는
`script_internal`이다. SceneManager가 준비한 이미지 소유권을 첫 renderer
frame packet에 넘기는 내부 인계이며, `PublishLiveFrame`의 제출 거부 시 복구와
frame 병합 시 보존, renderer의 exact-generation image demand로 이전한 뒤
`ReleaseImages`하는 과정이 하나의 수명 규약을 이룬다. 스크립트가 이를 먼저
소비하거나 교체하면 렌더러가 받아야 할 preload payload를 빼앗는다. 메서드만
있는 `TextureFramePins`를 `{}` JSON 스냅샷으로 돌려주는 것도 이 소유권을
전달하거나 복구하지 못한다. 비소비 조회인 `PreparedTextureImagePins`와 해당
live view의 `Size`, `Borrow`, `Owners`는 안전한 읽기 경로로 남긴다.

## F# 콜백 등록과 해제

`NativeCallbacks.Void`, `Float`, `Vector2`, `Text`, `Condition`은 각각
`void()`, `void(float)`, `void(math::vector2)`, `void(const std::string&)`,
`bool(const BlackBoard&)`에 맞는 `NativeCallback<'T>`를 만든다. 등록할 때
관리 컴포넌트를 지정하며 해당 컴포넌트 수명에 해제를 연결한다.
자동 해제의 소유자로 관리 스크립트 컴포넌트인 `this`를 전달한다. 별도로 조회한
네이티브 컴포넌트 래퍼는 관리 스크립트 registry가 수명을 추적하지 않으므로
명시적인 `Dispose` 또는 세대 종료로 해제해야 한다.
조건 콜백은 네이티브 BlackBoard 주소 대신 해당 호출의 읽기 전용 값 스냅샷을
받는다. 지원 서명에 맞는 메서드 인자에만 이 래퍼를 생성한다.

```fsharp
let button = entity.GetComponent<UIButton>()
let click = NativeCallbacks.Void(this, fun () -> printfn "clicked")
button.Native.SetClickFunction(click)
// 필요할 때 click.Dispose()로 해제할 수 있다.
```

관리 함수와 네이티브 dispatch delegate는 등록 기간 동안 root를 유지한다.
`Dispose`는 네이티브 복사본을 먼저 비활성화하고, 실행 중인 호출이 끝난 뒤
root를 해제한다. 콜백 안에서 자기 자신을 해제하는 경우에도 현재 호출의
root는 유지된다. 스크립트 재로딩은 이전 세대를 종료한 뒤 관리 어셈블리를
내린다. 콜백 실행 중 재진입한 세대 종료는 거절한다.

콜백 등록 경로가 엔진 기능의 구현을 추가하지는 않는다. 기존 ActionMap의
float 실행 분기와 StateMachine의 주석 처리된 Tick은 별도 엔진 기능 작업이다.
콜백 필드를 JSON으로 교체하거나 네이티브 에셋 I/O 콜백을 관리 코드로
넘기는 기능은 제공하지 않는다.

## 타입 정보와 쓰기 규약

`NativeReflection.Types`는 컴포넌트와 도달 가능한 타입의 전체 서술을 반환한다.
필드의 `CanRead` / `CanWrite`, 메서드의 `CanInvoke` / `CanBorrowResult` /
`CanBorrowWithArguments` / `CanInvokeBound`, `UnavailableReason` /
`BorrowUnavailableReason` / `BoundUnavailableReason`을
확인할 수 있다. 실제 경로의 읽기 전용 상태는
`NativeValue.Describe()`에서 확인한다.

이미 반영된 엔진 타입에는 `reflgen::public_methods`로 public 인스턴스 메서드를
추가한다. 이전에 반영되지 않았던 지형, 텍스처, 애니메이션, 오디오, 물리,
모델 값에는 `reflgen::runtime`을 사용한다. 이 표기는 별도의 runtime schema를
만들며 씬·프리팹·에셋의 기존 직렬화 스키마를 바꾸지 않는다.
public 참조 데이터 멤버는 getter 기반 필드 서술로 원래 이름과 참조를 유지한다.

쓰기는 파싱과 검증을 마친 뒤 적용하고,
`NotifyPropertyChanged(..., PropertyChangeSource::Script)`를 거친다.
읽기 전용 필드나 소유권이 섞인 레코드 전체 교체는 거절할 수 있다.
그 경우 필요한 writable 자식 필드를 선택한다. enable, owner, lifecycle처럼
불변 조건이 있는 상태는 해당 엔진 API로 변경한다.

`reflgen::script_internal`인 생명주기·소유자·신원 변경 훅은 메타데이터에 남지만
스크립트에서 직접 호출할 수 없다. 임의 네이티브 주소, SDK 객체, 등록되지 않은 콜백,
호출 수명으로 안전하게 표현할 수 없는 인자는 지원하지 않는 이유를 반환한다.
공개 템플릿 함수의 임의 인스턴스화는 런타임 호출 대상이 아니다.

## 수명과 전송

각 요청은 게임 스레드를 확인하고 엔티티 handle의 generation, 컴포넌트 인스턴스,
전체 멤버 경로를 다시 확인한다. 요청 동안만 GC root와 필요한 반환값 저장소를
유지한다. 컨테이너 재할당 뒤에도 같은 경로를 새 저장소에서 해석한다.

Execute는 응답 ticket을 먼저 확보한 뒤 요청을 한 번 실행한다. Read의 크기 조회와
버퍼 복사는 같은 결과만 읽는다. F#은 예외가 나도 `finally`에서 ticket을 해제한다.
요청은 1 MiB, 응답은 8 MiB, 경로는 64단계로 제한하며 큰 자산은 좁은 필드·원소
경로로 읽는다. Headless 바인딩도 반사와 콜백 슬롯 및 정확한 API 버전을 검사한다.

이 경로는 JSON 직렬화와 관리 할당이 있는 범용 API다. 매 프레임 반복되는 이동,
물리 등은 기존 전용 테이블 호출을 계속 사용할 수 있다.

## 검증

`Tools/regression/ScriptReflectionProbe`는 실제 `ScriptReflectionService.h`와
실제 `ScriptNative.fsproj`를 연결한다. 호스트의 컴포넌트 저장소만 fixture로
제공하며 생성 코드, 프로토콜, F# 전송은 제품 소스를 사용한다.
테스트는 Unicode/NUL, 중첩 컨테이너, 참조와 반환값, 오버로드, 읽기 전용 경로,
오래된 소유자·컴포넌트, 쓰기 알림, 전송 한계와 exactly-once 호출을 검사한다.
동일한 F# 생성기로 portable fixture의 타입별 구현도 생성해 실제 호출한다.
제품의 27개 컴포넌트와 모든 값 뷰는 실제 `ScriptNative.fsproj`에서 함께
컴파일되며, fixture 실행과 제품 전체 메서드 실행은 구분한다.

선언 변경 후에는 두 정본 모듈에서 새 선언 JSON을 추출하고 다음처럼 갱신한다.

```text
python Tools/build/generate-fsharp-native-facades.py --declarations <RenderEngine.declarations.json> --declarations <SceneRuntime.declarations.json>
python Tools/build/generate-fsharp-native-facades.py --check
```

관리 전용 빌드에는 정규화된 `native-surface.json`과 생성된 `.fs`를 사용한다.
Windows 계약 CI는 실제 헤더에서 새로 추출한 두 모듈의 선언으로 `--check`를
실행하므로, 네이티브 멤버가 변경됐는데 체크인된 F# 파일만 오래된 경우 실패한다.

`verify-native-reflection-inventory.py`는 native UUID/lifecycle 목록과 컴포넌트
선언을 대조한다. 실제 엔진에서 캡처한 카탈로그를 `--catalog`로 주면 컴포넌트와
재귀 타입 간 연결의 누락도 검사한다. 소스 목록 검사는 엔진 실행을 대체하지 않는다.

Windows 계약 CI는 고정된 reflgen 의존성으로 Debug/Release 네이티브·F# 경로를
검증한다. 전체 Editor/Player의 렌더링·오디오·물리 실행 결과는 별도 제품 회귀
검증으로 기록해야 한다. `verify-fsharp-native-product.ps1`와 수동
`native-reflection-product.yml`은 실제 호스트 PID, API 버전, 렌더링, 물리 이동,
오디오 출력 모드와 콜백 수를 확인한다. 필수 장치나 비공개 빌드 도구가 없으면
실패하며 계약 테스트의 성공을 제품 실행 성공으로 대신하지 않는다.
