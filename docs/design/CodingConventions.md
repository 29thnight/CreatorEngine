# 코드 컨벤션 — 두 층, 두 표기

> 2026-09-20 결정. §9의 네 항목에 답이 달렸고(9-1 (a) · 9-2 (b) · 9-3 (a) ·
> 9-4 제안대로) 그 결과가 본문에 반영돼 있다. 남은 것은 **이행**이다 — §8의
> 개명과 §7.3의 재포맷은 아직 실행되지 않았다. `.clang-format` 자체는
> 커밋됐고 미리보기도 끝났다(§7.2).

## 0. 이 문서가 정하는 것과 정하지 않는 것

**정한다**: 이름의 표기(casing), 접두·접미, 네임스페이스 대소문자, 파일 이름,
그리고 **어느 규칙이 어느 코드에 걸리는지 판정하는 기준**.

**정하지 않는다**: 설계 선택. "자체 컨테이너를 만들 것인가"는
[ContainerLibraryDesign.md](ContainerLibraryDesign.md)가, "무엇을 리플렉션에
올릴 것인가"는 [ReflectionRetentionDecision.md](ReflectionRetentionDecision.md)가
이미 답했다. 이 문서는 **만들기로 한 것의 이름을 어떻게 적는가**만 다룬다.

**정한다(9-3 결정)**: 포매팅 — 들여쓰기 문자, 중괄호 위치, 줄 길이를
`.clang-format`으로 고정한다. 기준값과 그 근거는 §7.1. 표기 규칙과 달리
이쪽은 **기존 파일 전부를 한 번에 건드리므로** 이행 절차가 따로 붙는다.

---

## 1. 규칙 둘

1. **엔진 객체는 C# 컨벤션으로 적는다.**
2. **엔진이 공용으로 쓰는 컨테이너·유틸리티 객체는 C++ STL 컨벤션으로 적는다.**

이 둘은 취향의 분할이 아니다. **각 층이 실제로 무엇과 나란히 읽히는가**가
다르기 때문이다.

- 엔진 객체는 `ScriptCore`의 C# 미러와 나란히 읽힌다. 그 대응은 이미 계약이다 —
  *"C# 래퍼는 네이티브 이름 그대로이고 예외가 없다(15종 대조 확인)"*
  ([ScriptSurfacePlan.md:75](../plans/ScriptSurfacePlan.md)). 네이티브가
  `GetComponent`면 C#도 `GetComponent`다. 두 표기를 쓰면 대조할 때마다
  옮겨 적어야 하고, 옮겨 적는 자리마다 어긋날 수 있다.
- 유틸리티·컨테이너는 `std`와 나란히 읽히고, 더 중요하게는 **`std`와 맞물려
  돈다**. range-for는 `begin`/`end`를, 구조적 바인딩은 `tuple_size`를,
  할당자는 `value_type`·`allocate`·`max_size`를 이름으로 찾는다. 표기를 바꾸면
  맞물리지 않거나, 맞물리는 부분만 예외가 되어 한 타입 안에 두 표기가 생긴다.

그리고 치환 비용 — 이 저장소가 이미 적어 둔 이유다:

> **STL 명명·시맨틱 유지**: 이유는 성능이 아니라 **치환 비용**이다.
> `InlineVector`가 `std::vector` 인터페이스 부분집합이었기 때문에 폐기 비용이
> 타입 별칭 한 줄이었다. 모양이 달랐으면 되돌릴 수도 없었다.
> — [ContainerLibraryDesign.md §2.1](ContainerLibraryDesign.md)

즉 층 B의 STL 표기는 **자체 제작물을 언제든 `std`로 되돌리기 위한 장치**다.
이 저장소는 실제로 `InlineVector`·`ce::` 컨테이너 별칭 20여 개·`MemoryPool`을
폐기해 봤고, 그때마다 치환 가능성이 비용을 갈랐다.

---

## 2. 어느 층인지 판정하는 기준

층은 디렉터리로 갈리지 않는다. `Engine/Utility_Framework/` 안에 두 층이
같이 살고, 심지어 **같은 파일 안에도 있다**(§5.1의 `ReflectionMeta.h`).

다음 질문에 순서대로 답한다. 먼저 걸리는 쪽이 그 타입의 층이다.

| # | 질문 | 예 → 층 |
|---|---|---|
| 1 | C# 미러가 있거나, 생길 수 있는가? | **A (C#)** |
| 2 | 리플렉션에 등재되어 인스펙터에 그려지거나 저작 자산으로 직렬화되는가? | **A (C#)** |
| 3 | 씬·엔티티·컴포넌트·에셋의 **수명이나 상태**를 지니는가? | **A (C#)** |
| 4 | `std` 기제에 물리는가 — range-for, `std` 알고리즘, 구조적 바인딩, 할당자 요구사항, 특성(trait) 탐지? | **B (STL)** |
| 5 | `std`의 무엇을 대신하거나 감싸는가? 또는 `std`로 되돌릴 여지가 있는가? | **B (STL)** |
| 6 | 타입 없는 자유 함수·템플릿 도구인가? | **B (STL)** |

어디에도 안 걸리면 **A**다. 층 A가 기본값인 이유는 엔진 코드의 절대다수가
객체이고, 판정이 애매한 것을 B로 보내면 §6의 어댑터 부담만 늘기 때문이다.

### 2.1 판정 예 (현 저장소)

| 타입 | 층 | 판정 근거 |
|---|---|---|
| `Entity` · `Component` · `Transform` · `Scene` | A | ①②③ 전부 |
| `Meta::Type` · `Meta::Property` · `Meta::Deserialize` | A | ② — 런타임 리플렉션이 다루는 대상이 객체다 |
| `AnimationController` · `Canvas` · `AudioService` | A | ②③ |
| `ce::remove_at_swap` · `ce::atomic_write` · `ce::read_file` | B | ⑥ — 자유 함수 |
| `meta::field` · `meta::schema` · `meta::for_each_field` | B | ④⑥ — 컴파일타임 도구 |
| `plf::colony` (서드파티) | B | ⑤ — `std` 컨테이너 자리 |
| `HashingString` | **B** | ⑤ — `std::string`의 자리에 놓인다 (§5.3) |
| `MemoryPool` | B | ④ — 할당자 요구사항에 물린다 |
| `WorkerPool` · `ThreadPool` | **A** | ③ — 수명을 지니는 서비스 객체다 (§5.2) |
| `LogSystem` · `CoreWindow` · `EnginePaths` | A | ③ |
| `BitFlag` | **B** | ⑤ — `std::bitset` 자리의 값 타입 (§5.3) |

★ 굵게 표시한 셋은 현재 표기와 판정이 어긋난다. §8에서 다룬다.

---

## 3. 층 A — 엔진 객체 (C# 컨벤션)

기준은 .NET 런타임 라이브러리 명명 지침이되, C++가 가진 것(포인터·참조·
const·연산자 오버로드)은 C++대로 적는다.

### 3.1 표기

| 대상 | 표기 | 예 |
|---|---|---|
| 클래스 · 구조체 · 열거형 | `PascalCase` | `AnimationController`, `GameObjectType` |
| 열거형 값 | `PascalCase` | `ScenePhase::Playing` |
| 메서드 | `PascalCase` | `GetComponent`, `AttachComponentLifecycle` |
| 공개 상수 | `PascalCase` | `Entity::SceneRootIndex` |
| 네임스페이스 | `PascalCase` | `Authoring`, `CommandService`, `Lifecycle` |
| 멤버 변수 | `m_camelCase` | `m_components`, `m_prefabOverrides` |
| 지역 변수 · 매개변수 | `camelCase` | `parentIndex`, `component` |
| 템플릿 매개변수 | `PascalCase` | `template<class TComponent>` |
| 파일 | 주 타입과 같은 이름 | `AnimationController.h` / `.cpp` |

### 3.2 C#과 다르게 두는 것

C#의 프로퍼티(`public int Count { get; set; }`)는 C++에 없다. `Get`/`Set`
접두 메서드로 적고, C# 미러 쪽에서 프로퍼티로 감싼다 — 지금 `ScriptCore`가
하고 있는 그대로다(`Native.GetName(Handle)` → `public string Name`).

`Engine/Utility_Framework/Core.Property.h`의 `Property<T>`는 이 규칙의 예외로
두지 않는다. 새 코드에서 쓰지 않는다 — `std::function` 두 개를 인스턴스마다
지므로 엔진 객체의 필드로 쓰면 비용이 조용히 붙는다.

### 3.3 접두 `m_`은 유지한다

C# 지침은 `_camelCase`를 권하지만 이 저장소는 `m_`이 1,650종이다
(`Engine`+`Editor` 헤더 기준). 바꿀
근거가 없다 — C# 미러와 대조되는 것은 **공개 표면**이고, 멤버 접두는 그
표면에 나타나지 않는다. 비용만 있고 이득이 없는 개명이다.

후치 언더스코어(`Delegate.h:17`의 `id_`)는 층 A에서 쓰지 않는다. 그 표기는
층 B의 것이다(§4.2).

### 3.4 상수 — `k` 접두를 유지하고 `UPPER_SNAKE_CASE`를 버린다

현재 표기가 갈려 있다. **같은 클래스 안에서도** 갈린다:

```cpp
// Engine/SceneRuntime/Entity.h:43, :49 — 여섯 줄 간격
static constexpr Entity::Index INVALID_INDEX   = ...;
static constexpr Entity::Index kSceneRootIndex = 0;
```

실태를 세면 갈림이 대등하지 않다 — **`k` 접두 332종, `UPPER_SNAKE` 15종**
(선언 기준, `Engine`+`Editor`). 22배다.

C# 컨벤션을 문자 그대로 적용하면 답은 `PascalCase`(`SceneRootIndex`)다.
그러나 이 한 항목은 **예외로 두고 `k` 접두를 공식 표기로 삼기를 제안한다.**
이유 둘:

1. C#에서 상수가 `PascalCase`여도 문제가 없는 것은 그 언어에 타입·메서드·
   프로퍼티를 가르는 다른 장치가 있기 때문이다. C++에서 `SceneRootIndex`는
   타입인지 상수인지 호출부에서 구분되지 않는다. `k`는 그 구분을 돌려준다.
2. 332종을 개명하는 diff는 이 문서의 나머지 전부를 합친 것보다 크다. 이득이
   "C# 지침과의 형식적 일치"뿐이라면 값을 못 한다.

버리는 쪽은 `UPPER_SNAKE_CASE` 15종이다. C++에서 그 표기는 **매크로의 것**이고,
매크로는 스코프를 무시하므로 상수가 같은 모양이면 충돌을 읽어 내기 어렵다.
15종이라 비용도 작다.

→ **확정(9-1 (a))**. 이것이 규칙 1번의 유일한 의도적 예외다. `k` 접두 332종은
그대로 두고, `UPPER_SNAKE` 15종만 `k` 접두로 옮긴다.

### 3.5 약칭을 쓰지 않는다

`SimulationTask`이지 `SimTask`가 아니다. `AnimationBehviourFatory`(현존
오타 파일명)처럼 줄이려다 틀린 이름이 남는다. 널리 통용되는 두문자어는
예외로 두되, 현 저장소 표기를 따라 대문자로 적는다(`ID`, `UUID`, `GUID`,
`RHI`, `UI`).

---

## 4. 층 B — 공용 컨테이너·유틸리티 (STL 컨벤션)

### 4.1 표기

| 대상 | 표기 | 예 |
|---|---|---|
| 클래스 · 구조체 | `snake_case` | `dynamic_array`, `hashing_string` |
| 함수 · 메서드 | `snake_case` | `push_back`, `remove_at_swap`, `atomic_write` |
| 멤버 변수 | `snake_case_` (후치) | `size_`, `capacity_` |
| 타입 별칭 | `snake_case` | `value_type`, `size_type`, `iterator` |
| 변수 템플릿 · 특성 술어 | `snake_case` + `_v` / `_t` | `is_container_v`, `element_t` |
| 네임스페이스 | `lowercase` | `ce`, `meta`, `rhi`, `assets`, `editor` |
| 템플릿 매개변수 | `PascalCase` 한 단어 | `template<class T, class Alloc>` |
| 파일 | 주 타입과 같은 이름, 그러나 **파일명만은 `PascalCase`** | `MetaSchema.h`가 `meta::schema`를 담는다 |

마지막 줄은 의도된 비대칭이다. 파일 이름은 Visual Studio 필터·`vcxproj`·
`#include` 경로에 박혀 있고 층과 무관하게 한 벌이어야 탐색이 된다. 층 B의
파일도 `PascalCase`로 적는다 — 이미 그렇다(`MetaSchema.h`, `ReflectionType.h`).

### 4.2 멤버 접두는 `m_`이 아니라 후치 `_`

층 B에서 `m_`을 쓰면 층이 이름에서 안 보인다. STL 구현체(libstdc++ `_M_`,
MSVC `_My`)는 예약 식별자를 쓰는데 그건 표준 라이브러리에만 허용된 영역이다.
사용자 코드에서 쓸 수 있는 관례가 후치 `_`다.

### 4.3 `std` 기제에 물리는 이름은 **반드시** 규격 그대로

`begin` · `end` · `size` · `empty` · `data` · `value_type` · `iterator` ·
`difference_type` · `swap` · `operator<=>`. 여기에 예외를 두면 그 타입은
range-for도 `std::ranges`도 타지 못한다. 이건 컨벤션이 아니라 **동작의
요구사항**이다 — 이미 저장소가 그렇게 하고 있다
(`AuthoringReadNode.h:176`의 `begin()`, `MemoryPool.h:33`의 `max_size()`).

이 항목이 규칙 2번의 진짜 뿌리다. 표기를 층으로 가르지 않으면 이런 타입은
**언제나 두 표기가 섞인다** — 규격이 강제하는 몇 개만 snake_case이고 나머지는
PascalCase인 상태. 층으로 가르면 섞이지 않는다.

### 4.4 자유 함수를 먼저 본다

`ce::remove_at_swap(vec, i)`는 새 타입 없이 `std::vector` 위에서 동작한다.
새 타입은 리플렉션 특수화·natvis·직렬화를 데리고 온다
([ContainerLibraryDesign.md §2.4](ContainerLibraryDesign.md)). 자유 함수로
되면 자유 함수로 둔다.

---

## 5. 경계에 걸린 것들

### 5.1 `Meta` vs `meta` — 오타가 아니다

```cpp
// Engine/Utility_Framework/ReflectionMeta.h
namespace meta { ... }   //  :18  — 컴파일타임 스키마 도구 (field, schema, adapt)
namespace Meta { ... }   // :177  — 런타임 타입 테이블 (Type, Property, TypeOf)
namespace meta { ... }   // :190  — 다시 컴파일타임
```

**한 파일 안에서 두 층이 교대한다.** 그리고 이 갈림은 §2의 기준과 정확히
일치한다 — `meta::field`는 `std::tuple`처럼 쓰이는 도구(④⑥)이고,
`Meta::Property`는 인스펙터가 그리는 객체(②)다.

즉 이 컨벤션은 저장소에 새 질서를 들이는 것이 아니라, **이미 암묵적으로
지켜지던 갈림을 문장으로 적는 것**이다. 이 둘은 통합하지 않는다.

남은 어긋남 둘:

- `meta::identity::StampIdentity()` (`ReflectionMeta.h:45`) — 층 B 안의
  PascalCase 메서드. `stamp_identity()`로 고친다. private이라 호출자가 둘뿐이다.
- `meta::displayName` (`MetaSchema.h:164`) — camelCase. `display_name`으로
  고친다. 같은 파일의 반환 타입 `display_name_attr`은 이미 snake_case다.

### 5.2 `WorkerPool`은 층 A다

이름에 "Pool"이 있지만 컨테이너가 아니다. `Startup`/`Shutdown`으로 수명을
지고 싱글턴이며 `std`의 무엇을 대신하지 않는다(③). 현 표기 그대로 둔다.

같은 이유로 `LogSystem` · `CoreWindow` · `DumpHandler` · `EnginePaths`도
층 A다. **`Engine/Utility_Framework/`에 있다고 층 B가 아니다.**

### 5.3 `HashingString` · `BitFlag`는 층 B다

둘 다 `std`의 자리에 놓이는 값 타입이다(`std::string` + 해시 캐시,
`std::bitset`). `HashingString`은 이미 `operator<=>`·`std::hash` 연동을
갖고 있고, 과거 결함도 `operator==`와 `operator<=>`의 **규격 불일치**였다
([ContainerLibraryDesign.md §1.2](ContainerLibraryDesign.md)). 규격에
맞물리는 타입이라는 증거다.

**확정(9-2 (b)) — 그러나 이유가 제안과 다르다.**

`BitFlag`는 개명한다. `BitFlag::Set`/`Test`/`Toggle`이 `bit_flag::set`/
`test`/`toggle`이 되고, 이는 `std::bitset`의 이름과도 같아진다. 사용처 3개
파일.

`HashingString`은 **개명하지 않는다 — 새로 작성하기 때문이다.** 이 타입은
이미 새로 쓰기로 예정돼 있고, 그러면 개명이라는 공정 자체가 없어진다. 새
타입이 층 B 표기(`hashing_string`)로 태어나고 호출부가 그쪽으로 옮겨 가면,
옛 타입은 사용처 0이 된 뒤 사라진다. 직렬화 경로를 건드리는 위험은 "이름을
바꾸는 일"이 아니라 "타입을 갈아 끼우는 일"의 위험으로 옮겨 가며, 그것은
이 문서가 아니라 그 교체 작업이 지는 몫이다.

따라서 §2.1 판정표의 `HashingString` 항목은 **예외가 아니라 예정**이다 —
지금은 층 A 표기이지만 그것이 정당해서가 아니라 아직 교체 전이라서다.
레거시 별칭(`using HashingString = hashing_string`)은 두지 않는다.

### 5.4 `ce::`는 층 B의 공용 함수 네임스페이스다

현재 276곳에서 쓰이고 전부 snake_case 자유 함수다(`ce::valid_name`,
`ce::atomic_write`, `ce::read_file`, `ce::encode`). 새 층 B 자유 함수는
여기 넣는다. `ce::dynamic_array`는 만들지 않기로 했으므로(설계 문서 결론)
`ce`는 **타입 없는 함수 네임스페이스**로 남는다.

### 5.5 네임스페이스 대소문자

`editor`(65) vs `Editor`(3), `meta`(6) vs `Meta`(21)처럼 갈린 곳이 있다
(헤더의 `namespace` 선언 횟수. `Meta` 쪽은 전방 선언이 여럿 포함된 수다).
§3.1·§4.1의 규칙을 그대로 적용한다 — **네임스페이스의 표기는 그 안에 든
것의 층을 따른다**. `editor::widgets`는 그리기 도구라 소문자가 맞고,
`Editor`(3건)는 층 A 객체를 담고 있으면 남기되 `editor`와 겹치면 하나로
모은다.

---

## 6. 두 층이 만나는 자리

층 A 객체가 층 B 컨테이너를 필드로 갖는 것은 정상이다. 규칙은 하나다.

**표기는 선언한 쪽을 따른다. 부르는 쪽에 맞춰 바꾸지 않는다.**

```cpp
class Entity : public Object          // 층 A
{
    void AddChild(Entity* child);     // A의 메서드 — PascalCase

    std::vector<Component*> m_components;   // B의 타입 — 그대로
};

// 호출부
for (auto* component : entity.GetComponents())   // A는 Get…, B는 begin/end
{
    ...
}
```

래퍼를 만들어 표기를 맞추지 않는다. `Entity::Size()`를 만들어
`m_components.size()`를 감싸는 식의 일은 하지 않는다 — 그 래퍼가 늘면 층
경계가 흐려지고, 컨테이너를 `std`로 되돌릴 때 §1의 치환 비용이 살아난다.

**예외 하나**: 층 A 객체가 range-for의 대상이 되어야 하면 `begin`/`end`를
층 B 표기로 노출한다. 규격이 그 이름을 요구하기 때문이다(§4.3). 그 두 개만
예외이고 나머지는 층 A 표기다.

---

## 7. 층과 무관한 공통 규칙

- **파일**: 헤더는 `.h`(617), 템플릿 구현 분리는 `.inl`(5). `.hpp`(6)은 새로
  만들지 않는다 — 두 확장자가 같은 뜻으로 쓰이고 있다.
- **헤더 가드**: `#pragma once`. 저장소의 헤더 617개 중 613개가 이미 그렇다
  (예외 4개: `pch.h` 둘, 서드파티 `plf_colony.h`, 리소스 `Resource.h`).
- **점이 든 파일명**(`Core.Property.h`, `Core.Thread.h`)은 새로 만들지 않는다.
  기존 것은 `#include` 경로에 박혀 있으므로 건드리지 않는다.
- **주석은 한글로 적고, 무엇을 하는지가 아니라 왜 그런지를 적는다.** 저장소
  주석의 강점이 이미 그것이다 — "이 길은 이미 깨져 본 길이다", "예전에는 넷으로
  갈렸다" 같은 기록이 재시도를 막는다.
- **개행**: 소스는 저장소 기본(`* text=auto`), 저작 자산과 셰이더는
  `.gitattributes`가 LF로 고정한다. `docs/`의 문서는 CRLF다.
- **인코딩**: UTF-8. CP949로 저장된 소스를 편집하면 한글 주석이 깨진다.
- **약칭 금지**(§3.5)는 층 B에도 걸린다. 단, STL이 쓰는 확립된 약칭은
  그대로 쓴다(`ptr`, `iter`, `alloc`, `impl`) — 규격과 나란히 읽히는 것이
  이 층의 목적이므로.

### 7.1 포매팅 — `.clang-format` (9-3 (a) 확정)

기준 도구는 **VS 2022 동봉 clang-format 19.1.5**
(`VC/Tools/Llvm/bin/clang-format.exe`)다. 스타일 기본값은 버전마다 바뀌므로
버전을 못 박는다. 값은 취향이 아니라 **실측으로** 골랐다 — 무작위 48개 파일에
후보 조합을 돌려 변경이 가장 적은 것을 취했다.

`BasedOnStyle: Microsoft`가 출발점이고, 넷은 이미 실태와 맞아 그대로 쓴다:

| 항목 | Microsoft 기본값 | 실태 근거 (`Engine`+`Editor`, 서드파티 제외) |
|---|---|---|
| `UseTab` | `Never` | 들여쓴 줄 20.2만 중 스페이스 15.6만(77%) |
| `IndentWidth` | `4` | 선행 공백의 **98.9%가 4의 배수**. 2칸 들여쓰기는 12줄뿐 |
| `ColumnLimit` | `120` | 120열 초과가 26.4만 줄 중 1,049줄(0.4%) |
| `BreakBeforeBraces` | `Custom` (Allman 계열) | 새 줄 여는 중괄호 23,220 대 442(**98%**) |

**여섯은 덮어쓴다.** 전부 `--dump-config`로 확인한 실제 기본값이고, 그대로
두면 재포맷이 의도와 다르게 동작한다:

| 항목 | 기본값 | 쓸 값 | 덮어쓰지 않으면 |
|---|---|---|---|
| `SortIncludes` | `CaseSensitive` | **`Never`** | ★★ include를 알파벳 순으로 **재정렬해 빌드를 깨뜨릴 수 있다** |
| `PointerAlignment` | `Right` | **`Left`** | 포인터·참조 선언 12,700여 곳이 뒤집힌다 |
| `ReflowComments` | `true` | **`false`** | 한글 주석이 전부 다시 접힌다 |
| `AllowShortFunctionsOnASingleLine` | `None` | **`Inline`** | 한 줄 인라인 함수 1,849곳이 세 줄로 펼쳐진다 |
| `BreakTemplateDeclarations` | `MultiLine` | **`Yes`** | `template<class T> void f();`로 합쳐진다 (실태 416 대 104) |
| `SpaceAfterTemplateKeyword` | `true` | **`false`** | `template<` 372 대 `template <` 148(72%) |

`SortIncludes`가 가장 위험하다. 이 저장소에는 선행 include에 의존하는 헤더가
있고(전방 선언·Windows 헤더·유니티 빌드), 재정렬은 포매팅이 아니라 **빌드를
깨뜨릴 수 있는 코드 변경**이다. 순서는 사람이 정한다.

`ReflowComments`: 이 저장소의 주석은 한글이고 분량이 많으며, 줄바꿈 위치가
뜻의 단위로 손수 맞춰져 있다. clang-format은 열 폭을 셀 때 한글의 2열 폭을
반영하지 못해, 재배치를 허용하면 **주석이 제자리에서 어긋난 채 전부 다시
접힌다.** 되돌릴 수 없는 손실이므로 끈다.

**`BinPackArguments`/`BinPackParameters`는 끄지 않는다.** 껐더니 실측에서
오히려 변경이 늘었다(2,943 → 3,153). 인자를 한 줄에 하나씩 세워 둔 자리
(예: `Entity::reflect()`의 `meta::field` 나열)는 전역 설정이 아니라 그 블록만
`// clang-format off` / `// clang-format on`으로 감싸 보호한다.

### 7.2 미리보기 결과 — 재포맷은 코드를 바꾸지 않는다

`.clang-format`을 커밋하기 전에 **전체 코퍼스 1,025개 파일**에 돌려 봤다
(`Engine`+`Editor`, 서드파티 제외).

| 지표 | 값 |
|---|---|
| 바뀌는 파일 | **994 / 1,025** (그대로인 파일 31) |
| 변경 줄 (추가+삭제) | 302,726 |
| 선행 공백을 무시한 변경 줄 | 101,201 |
| **모든 공백을 제거한 변경** | **0.230%** (559자 / 24.3만자, 48개 표본) |

줄 수만 보면 거대해 보이지만 **코드는 바뀌지 않는다.** 모든 공백을 지우고
문자 스트림으로 비교했을 때 남는 0.23%를 추적하니 전부 한 가지였다 —
`FixNamespaceComments`가 네임스페이스 닫는 `}` 뒤에 `// namespace Authoring`
같은 **주석을 붙이는 것**. 토큰은 하나도 바뀌지 않는다.

그래서 이 재포맷의 위험은 "코드가 변할까"가 아니라 "이력이 끊길까"뿐이고,
그것은 아래 절차로 처리된다.

**`ColumnLimit`을 0으로 두는 선택지도 쟀다.** 그러면 clang-format이 줄바꿈
위치를 건드리지 않아 변경이 101,201 → 66,210줄로 35% 줄고, 손으로 맞춰 둔
정렬이 보존된다. **코드 변화는 0.230%로 120일 때와 완전히 동일하다** — 이
선택은 안전성과 무관하고 "줄바꿈을 도구가 정하는가 사람이 정하는가"일 뿐이다.

`120`을 택했다. 코드가 안 바뀌는 것이 증명된 이상 diff 크기는 blame-ignore로
흡수되는 일회성 비용인 반면, `ColumnLimit: 0`은 포매터를 도입하고도 줄 길이
관리를 계속 사람이 지는 상태로 남기 때문이다. 손으로 맞춘 정렬은 전역 설정을
포기하는 대신 `// clang-format off`로 국소 보호한다.

### 7.3 이행 절차

순서를 지킨다:

1. `.clang-format`을 커밋한다 (포맷 변경 없음). — **완료**
2. 미리보기로 규모와 안전성을 확인한다. — **완료(§7.2)**
3. 전체 재포맷을 **단독 커밋 하나**로 만든다. 그 커밋에 코드 변경을 섞지 않는다.
4. 그 커밋 해시를 `.git-blame-ignore-revs`에 등재하고
   `git config blame.ignoreRevsFile .git-blame-ignore-revs`를 README에 적는다.
5. 재포맷 커밋의 **빌드 산출물이 이전과 같은지** 확인한다. §7.2가 토큰 불변을
   보였지만 그것은 표본 기준이고, 빌드는 전수다.

★ 3단계는 **작업 트리가 깨끗할 때** 해야 한다. 994개 파일을 건드리므로 다른
세션의 미커밋 변경이 있으면 통째로 휩쓸린다. 착수 전에 `git status`가 비어
있는지 확인한다.

---

## 8. 현재 실태와의 간극 — 그리고 이행

이 문서를 그대로 적용하면 개명 대상이 생긴다. 규모는 다음과 같다.

| 대상 | 규모 | 위험 |
|---|---|---|
| `meta::StampIdentity` → `stamp_identity` | 호출 2 | 없음 |
| `meta::displayName` → `display_name` | 호출 소수 | 없음 |
| `UPPER_SNAKE` 상수 → `k` 접두 | 15종 | 낮음 (컴파일러가 전부 잡는다) |
| `k…` 상수 332종 | — | **개명하지 않는다** (§3.4) |
| `BitFlag` → `bit_flag` | 3개 파일 | 중간 |
| `HashingString` | 12개 파일 | **개명 없음** — 새로 작성할 때 `hashing_string`으로 태어난다 (§5.3) |
| `Delegate.h`의 `id_` → 층 판정 후 정리 | 국소 | 낮음 |
| 전체 재포맷 (`.clang-format`) | **994 / 1,025개 파일** | 낮음 — 토큰 불변이 §7.2에서 확인됐다. 단 §7.3의 절차를 지킬 것 |

**이행 원칙 셋:**

1. **새로 쓰는 코드는 이 문서를 따른다.** 이건 오늘부터다.
2. **개명은 컴파일러가 전부 잡아 주는 것부터** 한다. 이름만 바뀌고 문자열로
   새어 나가지 않는 것 — 상수, private 메서드.
3. **직렬화·리플렉션에 이름이 새는 것은 개명하지 않는다.** 타입 이름이 저작
   자산이나 리플렉션 테이블에 문자열로 적히는 것은 개명이 곧 포맷 변경이다.
   그런 타입은 표기를 맞추려고 이름을 바꾸는 대신, 교체가 예정돼 있으면 그때
   새 표기로 태어나게 한다(`HashingString` → §5.3). 교체 예정이 없으면
   §2.1 판정표에 현 표기 그대로 기록하고 둔다.

이 저장소의 규칙상 레거시 호환 계층은 두지 않는다 — 옛 이름의 별칭을 남기는
식의 이행은 하지 않고, 바꿀 때 한 번에 바꾼다.

---

## 9. 결정 기록 (2026-09-20)

네 항목 모두 답이 달렸다. 본문은 이 결정을 반영한 상태다.

| # | 물었던 것 | 결정 | 본문 |
|---|---|---|---|
| 9-1 | 상수 표기 | **(a)** `k` 접두를 공식 표기로 인정. `UPPER_SNAKE` 15종만 정리 | §3.4 |
| 9-2 | `HashingString`·`BitFlag` 개명 | **(b)** `BitFlag`만 개명. `HashingString`은 새로 작성하며 층 B로 태어난다 | §5.3 |
| 9-3 | `.clang-format` | **(a)** 전면 도입 + 재포맷 단독 커밋 + blame-ignore 등재 | §7.1~7.3 |
| 9-4 | 게이트 강제 | 제안대로 — 9-1·9-2가 정해진 **뒤에** 만든다 (지금이 그 뒤다) | 아래 |

### 9-4 후속 — 게이트

9-1·9-2가 정해졌으므로 이제 만들 조건이 됐다. 형태는 `Tools/regression/`의
스크립트 하나이고, 최소한 다음 둘을 센다:

- 층 B 네임스페이스(`ce`, `meta`) 안의 PascalCase 식별자
- 층 A 헤더의 snake_case 공개 메서드 — 단 §4.3이 규격상 요구하는 이름
  (`begin`·`end`·`size` 등)은 허용 목록으로 뺀다

**허용 목록을 실태에서 뽑지 않는다.** 지금 있는 위반을 전부 허용 목록에
넣으면 게이트는 초록인 채 아무것도 막지 못한다. 허용 목록에 들어갈 자격은
"현재 존재한다"가 아니라 "§4.3이 요구한다"뿐이다. §8의 개명이 끝나기 전에
만든다면 남은 위반은 허용이 아니라 **알려진 적자**로 따로 세고, 수가 줄기만
하는지 본다.

초록이 뜨면 변이로 이빨을 확인한다 — 층 B 파일에 PascalCase 메서드를 하나
심어 붉어지는지, 층 A 헤더에 snake_case 공개 메서드를 심어 붉어지는지.

---

## 참고

- [ContainerLibraryDesign.md](ContainerLibraryDesign.md) — STL 기본값 원칙,
  치환 비용, 자체 컨테이너 판정 기록
- [ScriptSurfacePlan.md](../plans/ScriptSurfacePlan.md) — C# 미러의 명명 계약
- [ReflectionRetentionDecision.md](ReflectionRetentionDecision.md) — 리플렉션
  등재 판정
