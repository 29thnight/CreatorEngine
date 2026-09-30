# 엔진 리플렉션 — reflgen

작성: 2026-09-30 · 근거: `reflgen-adoption` 브랜치(f58d9fc0) · reflgen 85e2101(`ports/reflgen`)
이 문서는 엔진 리플렉션의 **현재 설계 정본**이다. 옛 체계(`reflect()` 레시피·`meta::schema`·`Meta::Type`)를
말하는 문서를 읽을 때는 §9의 대응표를 본다.

---

## 0. 한 문단

타입 서술의 정본은 **헤더의 선언 그 자체**다. `[[reflgen::reflect]]` 를 단 클래스의 non-static 데이터 멤버가
전부 반영되고, 빼려면 `[[reflgen::ignore]]` 를 단다. 빌드 때 reflgen 생성기(libclang)가 헤더를 읽어 서술을
만들고 모든 번역 단위에 강제 include 한다. 컴파일 때는 `reflgen::schema_of<T>`, 런타임에는 엔진이 소유한 등록소
`Meta::Types()` 의 `reflgen::type_descriptor` 로 읽는다. 씬·프리팹 YAML, 인스펙터, 콘솔 `object.property`,
Add-Component 목록이 모두 이 서술 하나를 읽는다. 저작 포맷은 옛 경로와 **바이트 동일**하다.

## 1. 타입을 서술하는 법

```cpp
// 예시 타입이다(저장소에 없다).
class [[reflgen::reflect]] DoorComponent : public meta::identity<DoorComponent, Component>
{
    friend struct reflgen::access;   // private 멤버를 서술이 읽는다
public:
    [[reflgen::reflect, creator::read_only_in_inspector]]
    bool IsOpen() const;

    [[reflgen::reflect]]
    void Toggle();                   // 인스펙터 메서드 버튼

private:
    EDoorKind m_kind = EDoorKind::Hinged;   // 반영된다 — 저장·인스펙터 대상

    [[reflgen::range(0.0f, 1.0f)]]
    float m_damping = 0.5f;

    [[reflgen::readonly, creator::debug_only]]
    HashedGuid m_lockID;             // 저장한다. 인스펙터는 디버그 모드에서 읽기 전용

    [[reflgen::ignore]]
    float m_openAmount = 0.0f;       // 런타임 상태 — 서술에서 뺀다
};
```

속성은 자기 줄에 두고 선언은 그 아래 줄에, 이어진 내용과는 빈 줄로 가른다 —
[CodingConventions.md](CodingConventions.md) §7.4(게이트 `verify-reflgen-attribute-layout.ps1`).

| 규칙 | 내용 |
|---|---|
| 반영 범위 | non-static 데이터 멤버 **전부**(opt-out). 옛 레시피는 opt-in이었다 — **필드를 새로 들이면 저장 포맷이 바뀐다.** 런타임 전용 상태에는 `[[reflgen::ignore]]` 를 단다 |
| 메서드 | `[[reflgen::reflect]]` 를 단 것만. 인스펙터 메서드 UI와 메뉴 스크립트 목록이 읽는다 |
| 부모 | `public` 부모 가운데 반영된 가장 가까운 조상. `meta::identity<T, Base>` 층은 건너뛴다 |
| 이름 | 서술 이름 = 엔진 타입 이름(`TypeTrait::type_name<T>()`). `[[reflgen::reflect("다른 이름")]]` 은 쓰지 않는다 — §3.2의 검사가 멈춘다 |
| enum | 필드의 enum은 `enum_descriptor` 로 인스펙터 콤보가 된다. YAML에는 정수로 적는다 |
| private 접근 | `friend struct reflgen::access;` |

`meta::identity<T, Base>` 는 서술과 무관하다 — 생성 때 `m_name`·`m_typeID` 를 찍는 스탬핑만 한다
(`ReflectionMeta.h`). `m_name` 은 씬 파일에 그대로 적히므로 reflgen 이름 표기와 엔진 표기가 같다는 것을
`static_assert` 로 확인한다.

### 1.1 속성

| 속성 | 읽는 곳 |
|---|---|
| `reflgen::ignore` | 서술에서 뺀다(엔진 전체 269곳) |
| `reflgen::hidden` · `reflgen::readonly` · `reflgen::display_name("…")` · `reflgen::range(lo, hi)` | 인스펙터 typed Draw(`ReflectionTypedDraw.h`) |
| `reflgen::transient` | 저장하지 않는다(`ReflgenAuthoringSerializers.h`) |
| `creator::debug_only` · `creator::wide` | 인스펙터 — 디버그 모드에서만 그린다 · 줄을 넓힌다 |
| `creator::read_only_in_inspector` · `creator::hide_in_inspector` | 인스펙터 메서드 UI — 인자 없는 메서드를 매 프레임 불러 결과만 보인다 · 그리지 않는다 |
| `creator::units("m")` | 저작 표기만 받는다(읽는 곳 없음) |

엔진 속성은 `Engine/Utility_Framework/ReflgenAttributes.h` 의 독립 타입이다(`creator` 이름공간 =
`ReflgenAttributeScopes`). 문자열 인자는 `reflgen::static_string` 으로 받는다 — 구조적 타입이라 C++26 주석
값이 될 수 있다. 새 속성은 이 파일에 더한다. 인자에 `creator::…` 같은 이름공간 이름을 쓰면 그 헤더의 생성물이
원본 헤더를 include 하므로(가벼운 주입을 잃는다, §2) 인자는 리터럴로 적는다.

### 1.2 새 타입을 들일 때

1. 헤더가 반영 모듈(`RenderEngine`·`SceneRuntime`·`Editor` — `Directory.Build.targets` 의
   `_EngineReflgenModule`)의 `ClInclude` 에 있어야 한다. 서술은 헤더를 `ClInclude` 로 가진 프로젝트가 만드는데,
   그 밖의 프로젝트에 두면 서술이 다른 프로젝트에 주입되지 않고(`ReflgenReference` 는 세 모듈만 잇는다) 그
   프로젝트의 등록 함수도 불리지 않는다. 새 모듈이 반영 타입을 가지면 `_EngineReflgenModule` 과 등록 호출을
   함께 더한다.
2. 컴포넌트·저작 레코드면 `RegisterReflectManual.h` 에 include와 `REFLECT_TYPE_LIST` 항목을 함께 더한다.
   이 목록이 typed 직렬화 훅(`TypeOps`)·에디터 typed Draw·아이콘 표·Add-Component 목록의 정본이다.
   등록소에는 반영 클래스가 전부 들어가므로(기반 `UIComponent` 까지) Add-Component 는 등록소가 아니라 이
   목록을 본다.
3. 서술이 직렬화하지 못하는 필드 타입은 컴파일 오류가 된다(reflgen `is_serializable`). 엔진 타입의 짝은
   `ReflgenAuthoringSerializers.h` 에 더한다.

## 2. 빌드 연동

reflgen은 vcpkg 매니페스트(`vcpkg.json`)의 overlay port(`ports/reflgen`)로 들어온다. port가 reflgen 저장소의
커밋을 가리키고, vcpkg가 생성기·헤더·MSBuild targets를 설치한다. `Directory.Build.targets` 가 그 targets를
모든 `.vcxproj` 에 가져온다.

| 설정 | 값 | 까닭 |
|---|---|---|
| `ReflgenAttributeScopes` | `creator` | 엔진 속성 이름공간을 서술로 옮긴다 |
| `ReflgenAttributeHeaders` | `ReflgenAttributes.h` | 주입 헤더가 맨 먼저 include 한다 — 생성물이 속성 타입을 원본 헤더 없이 본다 |
| `ReflgenRegistration` | `true` | 모듈마다 `reflgen::generated::register_<모듈>(registry&)` 를 만든다 |
| `ReflgenRegistrationHeaders` | `RenderEngine/ReflgenRegistration.h` | 등록 함수의 번역 단위가 엔진 serializer 특수화와 `Material`(`MeshRenderer.h` 는 전방 선언만 한다)을 보게 한다 |
| `ReflgenReference` | 세 모듈 서로 | 생성만 잇는 참조(빌드 순서를 걸지 않는다, 순환 허용 — 엔진 라이브러리는 서로의 헤더를 경로로 include 한다) |

- **주입은 전방 선언뿐이다.** 서술은 타입이 완전한 자리에서 실체화된다. 헤더 하나를 고쳐도 그것을 include 하는
  번역 단위만 다시 컴파일된다. 원본 헤더를 끌어오게 되는 경우(클래스 안 타입, 이름공간 이름을 쓰는 속성 인자
  등)는 생성물에 그 까닭이 주석으로 남는다.
- **모든 native 프로젝트가 세 모듈의 주입을 받는다.** 한 번역 단위라도 주입 없이 반영 타입을 보면
  `reflgen::reflectable<T>` 가 번역 단위마다 달라진다 — 조용한 ODR 위반이다.
- 등록 함수의 번역 단위는 참조 프로젝트의 주입도 받는다(`reflgen_<모듈>.references.h`, reflgen 5357b1b).

### 2.1 reflgen을 다른 커밋에 고정할 때

1. reflgen 저장소에서 `scripts/make-overlay-port.ps1 -Ref <sha>` 로 port를 만들어 `ports/reflgen` 을 바꾼다.
2. 첫 빌드는 vcpkg가 reflgen을 다시 설치한 뒤 **"Build again"** 오류로 멈춘다. 평가 때 가져온 targets가 낡았기
   때문이다(`_EngineReflgenUpdatedDuringBuild`). 두 번째 빌드가 새 reflgen으로 끝까지 간다.
3. vcpkg MSBuild 연동은 원래 `vcpkg.json` 이 바뀔 때만 다시 설치한다. 엔진은 `ports\**` 를 설치 입력에 더해
   port 교체도 잡는다. 그래도 설치된 헤더가 실제로 바뀌었는지 한 번 확인하고 믿는다.

reflgen을 고치면서 엔진에 바로 걸어 볼 때는 `EngineReflgenTargets` 를 reflgen 저장소의 `msbuild\reflgen.targets`
로, `ReflgenExecutable` 을 빌드한 생성기로 준다.

## 3. 런타임 — 등록소와 창구

### 3.1 등록소

`Meta::Types()` 가 프로세스에 하나인 `reflgen::registry` 다. 정의는 `ReflgenRuntime.cpp` 에 둔다 — 헤더 인라인
싱글턴은 금지다(`verify-header-inline-singleton.ps1`). `reflgen::default_registry()` 는 헤더 인라인이라 엔진을
DLL로 떼면 모듈마다 갈리므로 쓰지 않는다. 채우는 곳:

- `RegisterReflectManual()` — `register_RenderEngine` · `register_SceneRuntime`, 그리고 `REFLECT_TYPE_LIST` 의 `TypeOps`
- 에디터 초기화(`EditorMain`) — `register_Editor`

### 3.2 정체성

엔진 typeID와 reflgen `type_id` 는 **같은 값**이다 — 둘 다 한정 이름의 FNV-1a 64다. `RegisterReflectManual.h`
가 `REFLECT_TYPE_LIST` 의 타입마다 두 가지를 컴파일 때 대조한다: `type_id` 가 엔진 typeID와 같은지, 서술 이름이
`TypeTrait::type_name<T>()` 와 같은지. 표기가 갈리면 조용한 "미등록"이 아니라 빌드가 멈춘다. 이름 표기는
reflgen 표기다(`std::basic_string<char,std::char_traits<char>,…>` 등) — 콘솔 `object.properties` 의 타입 이름도
이 표기로 나온다.

### 3.3 창구 — `ReflgenRuntime.h`

| 함수 | 뜻 |
|---|---|
| `Meta::Find(name)` · `Meta::Find(HashedGuid)` | 이름·typeID로 서술자 |
| `Meta::TypeOf<T>()` · `Meta::Require(id, name)` | 등록돼 있어야 하는 타입 — 없으면 예외(등록 누락) |
| `Meta::TypeIDOf(type)` | 서술자 → 엔진 typeID |
| `Meta::Parent(type)` | 가장 가까운 반영된 부모 |
| `Meta::LocalFields(type)` · `Meta::LocalMethods(type)` | 그 타입이 선언한 것만. `fields()`·`methods()` 는 부모 것까지 부모 우선 |
| `Meta::Create<Base>(type)` | 새 인스턴스를 `Base` 로 — 포인터 보정은 서술자의 base 체인이 한다 |
| `Meta::MostDerived(object)` | 다형 객체의 실타입 주소 |

소비자는 **`reflgen::type_descriptor_of<T>()` 를 직접 부르지 않는다.** 그 번역 단위에서 서술자를 다시 만들고(컴파일
비용), 엔진 특수화를 못 보는 번역 단위라면 다른 서술자가 된다(ODR 위반). 등록소에서 찾기만 한다.

**포인터 계약**: 서술자의 `serialize`·`deserialize`·필드 `address()`·메서드 `invoke()` 는 **그 서술자 타입의
포인터**를 받는다. 엔진 객체를 넘기는 `IObject*` 오버로드(`SerializeInto`·`SerializeDocument`·`Deserialize`·
`DeserializePrefab`)는 `ObjectAddress` 로 주소를 맞춘다 — 실타입 주소에서 실타입 서술자의 base 체인을 따라
올라간다. `Component*`·`Entity*` 인자는 `void*` 보다 이 오버로드로 가므로 호출처가 보정을 잊을 수 없다.

## 4. 소비자

| 소비자 | 경로 |
|---|---|
| 씬·프리팹·복제 YAML | `ReflectionYml.h` — `type.serialize/deserialize` 를 Authoring 노드 위의 `ReflgenWriter`/`ReflgenReader`(`ReflgenAuthoring.h`)로 부른다. 레코드 봉투(훅·헤더·레거시 건너뛰기 규칙·`OnPropertyChanged`·수학 타입 flow map·포인터 필드)는 `ReflgenAuthoringSerializers.h` |
| 역직렬화 후처리 | `TypeOps` 에는 `postLoad` 훅만 남는다. 쓰기·읽기는 서술자가 한다 |
| 설정 자산 | `RuntimeSettings`·`EditorSettingsStore` 는 등록 전에 읽고 쓰므로 컴파일 때 경로(`Meta::Typed::SerializeObjectInto`·`DeserializeObjectFrom`)를 쓴다 |
| 인스펙터 | typed Draw(`ReflectionTypedDraw.h`)가 `reflgen::schema_of`·`direct_bases_t`·속성을 읽는다. 메서드 UI는 `method_info::invoke`(`ReflectionImGuiHelper.h`), enum 콤보는 `enum_descriptor` 가 기반 폭에 맞춰 읽고 쓴다 |
| 콘솔 `object.property` | `EditorObjectOperations.cpp` — 필드 주소에 타입별로 쓴다. enum은 이름과 값을 모두 받는다 |
| Add-Component | `REFLECT_TYPE_LIST` 의 타입(§1.2) |

**`DeserializePrefab` 은 그 타입이 선언한 필드(`LocalFields`)만 프리팹 값으로 바꾼다.** 옛 경로도 결과가 같았다
— 부모 단계를 먼저 읽었지만 마지막 읽기가 그 전에 뜬 스냅샷으로 부모 필드(`m_name`·`m_instanceID` 등)를
되돌렸다. 부모 필드까지 받으면 인스턴스가 원본의 이름과 instanceID를 받는다(`verify-prefab-override-write` 가
잡는다).

## 5. 검증

| 무엇 | 어떻게 |
|---|---|
| 저장 포맷 | 기준선(전환 전 master) 에디터와 같은 장면을 열고 저장해 바이트 비교 — 컴포넌트 25종 fixture·`FT_Primitives`·`ProfilingWorkerFixture`·자산 코퍼스 장면 |
| 프리팹·플레이 | `verify-prefab-{roundtrip,override-write,nested,nested-update,duplicate,identity-injection}` · `verify-play-roundtrip` |
| 컨테이너 | `verify-reflection-container`(빈 컨테이너 래칫은 `ReflgenAuthoring.h` 를 본다) · `verify-reflection-container-roundtrip`(28축, `reflection_container_roundtrip_probe.cpp`) |
| 인스펙터 | `verify-inspector-drawer-layout` · `verify-inspector-spatial-policy` |
| 컴파일 때 | §3.2 정체성 대조, `meta::identity` 이름 대조, reflgen 직렬화 가능성 검사 |
| 기동 때 | `VerifyReflectRegistration()`(`RegisterReflectManual.h`) — `REFLECT_TYPE_LIST` 의 서술자가 쓰고 읽을 수 있어야 한다. 등록 함수가 serializer 특수화를 못 보면 critical 로그 후 abort |
| 배치 | `verify-reflgen-attribute-layout.ps1` — 속성 배치 규약(CodingConventions.md §7.4) |

## 6. 함정

- **필드 추가 = 포맷 변경.** opt-out이므로 새 멤버는 기본으로 저장된다. `[[reflgen::ignore]]` 를 떼는 것도
  같다. 전환 때 옛 레시피에 없던 필드에는 codemod(`Tools/migration/reflgen_codemod.py`)가 `ignore` 를 붙였다 —
  `RigidBodyComponent::AngularDamping` 처럼 레시피 누락이 그대로 옮겨진 것도 있다.
- **전방 선언만 된 필드 타입.** 서술자는 등록 함수의 번역 단위에서 만들어지므로 그곳이 타입을 완전히 봐야
  한다. 헤더가 전방 선언만 하는 타입은 `ReflgenRegistration.h` 에 include를 더한다. reflgen은 아직 이 경우를
  분명하게 진단하지 않는다.
- **컴파일 시간.** 직렬화 본문을 번역 단위마다 실체화하지 않는다 — 소비자는 등록소의 서술자를 부른다. 전환 뒤
  Editor 빌드(Debug x64, `/m`): clean 342.9 s → 308.6 s, `Transform.h` 수정 185.1 s → 136.3 s.
- **생성기가 다루지 않는 것**: 클래스 템플릿, 오버로드된 반영 메서드, enumerator 속성, union. 진단으로 멈춘다.
- **`&&`·`volatile`·가변 인자 메서드**는 `schema_of` 에는 있지만 런타임 `methods()` 에서는 빠진다(부를 수 없다).

## 7. 기각한 대안

| 대안 | 기각 근거 |
|---|---|
| 엔진 스키마(`MetaSchema.h`)를 정본으로 두고 reflgen을 다리(`meta::of<T>`)로 잇기 — P3·P4의 형태 | 스키마가 두 벌이고 둘이 같다는 것을 대조표로 지켜야 했다(필드를 고칠 때마다 깨졌다). 다리가 번역 단위마다 스키마를 옮기는 비용도 컸다 — 다리·엔진 스키마·직렬화 썽크를 걷은 P5 마지막 단계 전후로 `Transform.h` 수정 빌드가 177.6 s → 136.3 s |
| 생성물이 원본 헤더를 include 하는 주입 | P0에서 Editor 빌드 3.1배, `windows.h` 의 `near` 매크로가 번역 단위 하나를 깨뜨렸다. 전방 선언만 주입한다 |
| `reflgen::default_registry()` | 헤더 인라인이라 엔진을 DLL로 떼면 모듈마다 등록소가 갈린다. 헤더 인라인 싱글턴 금지 규약에도 걸린다 |
| `TypeOps` 가 타입마다 직렬화 썽크를 들기 | `SceneManager.cpp` 가 76타입의 직렬화를 따로 실체화했다(48.5 s → 서술자 경유 16.0 s). 썽크를 둔 채로는 clean 빌드가 기준선보다 9.6% 느렸다 |
| 소비자가 `type_descriptor_of<T>()` 를 직접 부르기 | 서술자를 번역 단위마다 다시 만들고, 엔진 특수화를 못 보는 곳에서는 다른 서술자가 된다(§3.3) |
| `DeserializePrefab` 이 부모 필드까지 바꾸기 | 인스턴스가 원본의 `m_name`·`m_instanceID` 를 받는다. 옛 경로의 결과(선언한 필드만)와 같게 둔다(§4) |
| Add-Component 목록을 등록소에서 뽑기 | 등록소에는 반영 클래스가 전부 들어가 기반 `UIComponent` 까지 이름 규칙에 걸린다. `REFLECT_TYPE_LIST` 를 본다 |
| 엔진 YAML emitter를 유지하고 서술만 reflgen에서 받기 | 직렬화 규칙이 두 곳에 있게 된다. 엔진 YAML은 reflgen 직렬화 위의 writer·reader로 쓰고, 출력은 바이트 동일로 맞췄다(2026-09-28 결정) |

## 8. 결정 이력

- 2026-08-07 [ReflectionRetentionDecision.md](ReflectionRetentionDecision.md) — 엔진 리플렉션 존치. 근거(자산
  포맷·핫리로드·인스펙터 자동 생성)는 지금도 유효하고, reflgen 전환은 세 가지를 모두 유지한다.
- PHASE 18 [ReflectionRedesignPlan.md](../plans/archive/ReflectionRedesignPlan.md) — 매크로를 걷고
  `static consteval auto reflect()` 레시피로 옮겼다.
- 2026-09 reflgen 전환(P3 파일럿 → P4 전체 변환 → P5 소비자) — 레시피를 헤더 속성으로 옮기고
  (`reflgen_codemod.py`: 79타입, `ignore` 269곳), YAML을 reflgen 직렬화로, 런타임 타입 표와 인스펙터를 reflgen
  서술로 옮긴 뒤 엔진 스키마(`MetaSchema.h`)와 다리(`ReflgenBridge.h`)를 걷었다.

## 9. 옛 이름 대응

| 옛 | 지금 |
|---|---|
| `static consteval auto reflect()` + `meta::schema<Self>(meta::field<&Self::x>, …)` | `class [[reflgen::reflect]] T` + 멤버 선언(빼는 것에 `[[reflgen::ignore]]`) |
| `meta::field<…>.with(attr)` | 멤버에 `[[attr]]` |
| `meta::method<…>` · `Meta::MakeMethod` | 메서드에 `[[reflgen::reflect]]` · `reflgen::method_info` |
| `meta::schema_of<T>` · `meta::of<T>` | `reflgen::schema_of<T>` |
| `meta::displayName` · `meta::units_attr` 등 | `reflgen::display_name` · `creator::units` 등 |
| `Meta::Type` · `Meta::Property` · `Meta::Method` | `reflgen::type_descriptor` · `field_info` · `method_info` |
| `Meta::Registry` · `RegisterClassInitalize` | `Meta::Types()` · 생성된 `register_<모듈>` |
| `Meta::Find` · `Meta::TypeOf` | 같은 이름, 서술자를 돌려준다(`ReflgenRuntime.h`) |
| `create_enum_type` · `Property::enumType` | `reflgen::enum_descriptor_of<E>()` · `field_info::enumeration()` |
| `MetaSchema.h` · `ReflgenBridge.h` · `ReflgenParity.h` | 없음 |
