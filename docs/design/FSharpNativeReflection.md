# F# 네이티브 컴포넌트와 값 그래프 바인딩

## 목적과 진입점

API 48은 기존 네이티브 테이블 끝에 `NativeReflection_Execute`,
`NativeReflection_Read`, `NativeReflection_Release` 세 슬롯을 추가한다.
F# `CreatorEngine.NativeBindings`가 이 슬롯을 직접 호출한다.
컴포넌트마다 F# 래퍼를 작성해야만 멤버에 접근할 수 있었던 제약을 공통
`NativeObject` / `NativeValue` 경로로 보완한다.

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
이름은 네이티브 서술자의 이름을 사용한다.

## 타입 정보와 쓰기 규약

`NativeReflection.Types`는 컴포넌트와 도달 가능한 타입의 전체 서술을 반환한다.
필드의 `CanRead` / `CanWrite`, 메서드의 `CanInvoke` / `CanBorrowResult`,
`UnavailableReason`을 확인할 수 있다. 실제 경로의 읽기 전용 상태는
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
스크립트에서 직접 호출할 수 없다. 임의 네이티브 주소, SDK 객체, callback,
호출 수명으로 안전하게 표현할 수 없는 인자는 지원하지 않는 이유를 반환한다.
공개 템플릿 함수의 임의 인스턴스화는 런타임 호출 대상이 아니다.

## 수명과 전송

각 요청은 게임 스레드를 확인하고 엔티티 handle의 generation, 컴포넌트 인스턴스,
전체 멤버 경로를 다시 확인한다. 요청 동안만 GC root와 필요한 반환값 저장소를
유지한다. 컨테이너 재할당 뒤에도 같은 경로를 새 저장소에서 해석한다.

Execute는 응답 ticket을 먼저 확보한 뒤 요청을 한 번 실행한다. Read의 크기 조회와
버퍼 복사는 같은 결과만 읽는다. F#은 예외가 나도 `finally`에서 ticket을 해제한다.
요청은 1 MiB, 응답은 8 MiB, 경로는 64단계로 제한하며 큰 자산은 좁은 필드·원소
경로로 읽는다. API 48 Headless 바인딩도 이 세 슬롯을 필수로 검사한다.

이 경로는 JSON 직렬화와 관리 할당이 있는 범용 API다. 매 프레임 반복되는 이동,
물리 등은 기존 전용 테이블 호출을 계속 사용할 수 있다.

## 검증

`Tools/regression/ScriptReflectionProbe`는 실제 `ScriptReflectionService.h`와
실제 `ScriptNative.fsproj`를 연결한다. 호스트의 컴포넌트 저장소만 fixture로
제공하며 생성 코드, 프로토콜, F# 전송은 제품 소스를 사용한다.
테스트는 Unicode/NUL, 중첩 컨테이너, 참조와 반환값, 오버로드, 읽기 전용 경로,
오래된 소유자·컴포넌트, 쓰기 알림, 전송 한계와 exactly-once 호출을 검사한다.

`verify-native-reflection-inventory.py`는 native UUID/lifecycle 목록과 컴포넌트
선언을 대조한다. 실제 엔진에서 캡처한 카탈로그를 `--catalog`로 주면 컴포넌트와
재귀 타입 간 연결의 누락도 검사한다. 소스 목록 검사는 엔진 실행을 대체하지 않는다.

Windows 계약 CI는 고정된 reflgen 의존성으로 Debug/Release 네이티브·F# 경로를
검증한다. 전체 Editor/Player의 렌더링·오디오·물리 실행 결과는 별도 제품 회귀
검증으로 기록해야 한다.
