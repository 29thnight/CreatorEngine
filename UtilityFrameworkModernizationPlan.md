# 유틸리티 프레임워크 현대화 — 표준 회귀와 죽은 코드 정리 (PHASE 15)

작성: 2026-08-14 · 계기: `Core.Assert.hpp` 제거 논의. "사용하는 곳이 없는데 없애도
되지 않을까"에서 출발해 전수 조사로 확대했고, 같은 성격의 것이 더 있다는 것이 실측으로
확인됐다. 이어서 `HashingString` 분석 결과를 **트랙 H**로 편입했다.

관련 문서: `Phase4CouplingPlan.md`(간선 절단 — 우산 헤더 문제를 공유),
`SceneGraphRedesignPlan.md`(트랙 H의 §6 항목이 그쪽 트랙 E의 `GetGameObject` 60곳과 겹친다),
`BuildPipelinePlan.md`(트랙 구조·게이트 관례를 승계).

---

## 0. 전략 요약

**세 갈래를 구분한다.** "정리"라는 한 단어로 뭉뚱그리면 위험도가 전혀 다른 작업이 섞인다.

- **죽은 코드** — 소비자가 실제로 0인 것. 지우면 끝이고 회귀 위험이 거의 없다.
- **표준으로 회귀** — C++20 표준이 제공하는데 직접 구현해 둔 것. 프로젝트는 이미
  `stdcpp20`이다(`Utility_Framework.vcxproj:255`). 자작 구현이 표준보다 나은 근거가
  없다면 표준이 옳다. **단, 동작이 같음을 확인한 뒤에만 옮긴다.**
- **결함 수정** — 살아 있는데 잘못 만들어진 것. HashingString과 `Core.Runtime.h`가 여기다.
  이건 정리가 아니라 수리이고, 우선순위가 가장 높다.

**순서의 원칙: 지우는 것이 먼저, 옮기는 것이 나중.** 죽은 코드를 먼저 걷어내야 표준
대체 작업의 표면이 줄어든다. 반대로 하면 곧 지울 것을 이식하는 데 시간을 쓰게 된다.

**단, §1.1의 UB 한 건은 이 순서 밖이다.** 표준 라이브러리의 계약을 깨는 코드가
거의 전 코드베이스에 퍼져 있고, 이건 정리가 아니라 지혈이다.

---

## 1. 실측 — 2026-08-14 전수 조사

방법: 프로젝트 소스 1,185개(vcpkg·빌드 산출물 제외)에서 `#include` 그래프를 만들고,
각 유틸리티의 주요 타입명을 **선언 파일 자신을 제외한** 나머지 전체에서 역추적했다.
include 수만 세면 `Core.Minimal.h`가 전이로 끌어오는 것들이 전부 "미사용"으로 잘못
잡히기 때문에, 심볼 소비자를 기준으로 판정했다.

Utility_Framework: **73개 파일**.

### 1.1 표준 라이브러리 네임스페이스 침범 — 최우선

`Core.Runtime.h` 전문이 이것이다:

```cpp
namespace std
{
    class null_exception : public std::exception { ... };
}
```

`namespace std`에 사용자 선언을 추가하는 것은 **정의된 동작이 아니다**([namespace.std]).
표준이 허용하는 것은 사용자 타입에 대한 일부 템플릿 특수화뿐이고, 새 클래스 추가는
여기에 해당하지 않는다. 구현체가 같은 이름을 도입하면 그 자리에서 깨지고, 그때
에러 메시지는 원인 지점을 가리키지 않는다.

여파가 넓다 — `Core.Runtime.h`는 `Core.Minimal.h:4`에 들어 있고, `Core.Minimal.h`는
사실상 전 코드베이스의 진입 헤더다. 즉 **이 UB가 거의 모든 번역 단위에 있다.**

실사용은 좁다(같은 날 실측): `Component.cpp:12`·`:20` 두 곳뿐이고, `Component.inl:9`·`:25`는
주석 처리되어 있다. **표면이 2줄이라 S0의 실제 작업량은 매우 작다** — 위험도가 낮은데
여파가 큰, 먼저 처리해야 할 전형적인 항목이다.

### 1.2 소비자가 실제로 0인 것

선언 파일을 제외한 전체 소스에서 참조 0건:

| 항목 | 파일 | 크기 | 비고 |
|---|---|---|---|
| `FileReader` / `FileWriter` | `FileIO.h` | 2.3KB | 파일 전체가 죽었다 |
| `ComException` | `DirectXHelper.h` | 2.2KB | DX11 시절 잔재 |
| `Aes256Ctr` · `LZ4Codec` | `Paklib.hpp` 내부 | — | §1.5 참조 |
| `RenderCommandFence` · `RHICommandFence` | `EngineSetting.h:129-130` | — | 멤버 선언만 있고 호출 0 |

`Paklib.hpp` 자체는 살아 있다 — `PakHelper.h:155·465`가 `Pak::Builder`/`Pak::Archive`를
쓴다. 죽은 것은 그 안의 암호화·압축 코덱이다.

### 1.3 소비자가 1곳뿐인 것 (표준 대체 후보)

| 유틸 | 유일한 소비자 | C++20 표준 대응 |
|---|---|---|
| `Core.CountingSemaphore.h` | `Core.ThreadPool.h` | `std::counting_semaphore` |
| `MemoryPool.h` / `.inl` (6.7KB) | `Core.Memory.hpp` | — (표준 없음, 존치 판단) |
| `RingBuffer.h` | `LogSink.h` | — (표준 없음, 존치 판단) |
| `MetaStateCommand.h` | `ReflectionFunction.h` | — |
| `CoroutineHelper.h` (`YieldInstruction`) | `Core.Coroutine.h` | — |
| `LinkedListLib.hpp` | `Core.Coroutine` 2곳 | `std::list` 검토 |

소비자 1곳은 "죽었다"가 아니라 **"추상화가 값을 못 하고 있다"**는 신호다. 재사용을
전제로 만든 일반 유틸리티인데 쓰는 곳이 하나면, 그 하나에 인라인하는 편이 나을 수
있다. 개별 판정이 필요하다.

### 1.4 살아 있는 것 — 표준 대체 검토 대상

| 유틸 | 외부 소비자 | 표준 대응 | 판정 |
|---|---|---|---|
| `Core.Barrier.h` (`Barrier`) | 0 — 3-2G에서 삭제 | — | **은퇴 완료** — 아래 |
| `Core.Thread.h` (`Thread`) | 9파일 | `std::jthread` + `std::stop_token` | 유력 |
| `Core.ThreadPool.h` | 7파일 | — | 존치 |
| `WorkerPool.h` | 3파일 | — | 존치 |
| `SpinLock.h` | 55회 / 5파일 | — | 존치 |
| `Singleton` (`ClassProperty.h`) | 56회 / 24파일 | — | 존치 |

`Barrier`는 당시 역할·페이즈·`Finalize()` 의미 때문에 `std::barrier`로 1:1 교체할
수 없었다. 3-2G에서 프레임 스냅샷 producer/consumer 전환이 완료되자 교체할 필요
자체가 사라졌다. 빈 CommandBuild 스레드와 Game/CB/CE 랑데뷰, syncstats CLI,
프로젝트 항목을 함께 제거하고 GT→Presentation 단방향 latest-wins 통지로 바꿨다.

`Core.Thread.h`는 `Start`/`Stop`/`RequestStop`/`Join` + `std::atomic<bool>` 정지 플래그로,
`std::jthread`의 `stop_token`과 의미가 거의 같다. 여기가 가장 이식하기 쉽다.

### 1.5 문서와 구현의 불일치

`README.md:70`은 이렇게 적고 있다:

> 팩 파일 시스템: `Paklib` 헤더는 LZ4 압축 훅, AES-256-CTR 암호화, SHA-256 무결성
> 검사를 갖춘 패키징 런타임을 제공합니다.

그런데 `Paklib.hpp:7`은 이렇게 적고 있다:

> LZ4 사용: 프로젝트에 LZ4를 추가하고, 아래 Pak::Compression::LZ4Codec 구현의
> TODO 표시된 부분을 채우세요.

`vcpkg.json:43`에 lz4 의존성은 이미 있는데 코덱은 TODO 상태이고, 소비자도 0이다.
`EngineStructureAnalysis.html:292`와 `:650`도 README와 같은 주장을 반복한다.
**빌드된 pak이 압축·암호화되지 않는다는 뜻이며**, 배포 산출물의 성질에 관한
문제이므로 `EnginePackagingPlan.md`와 교차 확인이 필요하다.

### 1.6 껍데기 헤더

| 파일 | 내용 |
|---|---|
| `Core.Thread.hpp` (42B) | `#include "Core.ThreadPool.h"` 한 줄 |
| `Reflection.hpp` (42B) | `#include "ReflectionMecro.h"` 한 줄 |

이름이 가리키는 것과 내용이 다르다 — `Core.Thread.hpp`를 열면 스레드가 아니라
스레드 풀이 나오고, `Core.Thread.h`(다른 파일)와 확장자 하나로 갈린다.
`ReflectionMecro.h`는 철자도 틀려 있다(Mecro → Macro).

### 1.7 우산 헤더

`Core.Minimal.h`는 9개 헤더를 끌어온다 — `Core.Definition.h`, `Core.Mathf.h`,
`Core.Runtime.h`, `Core.Memory.hpp`, `Core.Coroutine.h`, `PathFinder.h`,
`LogSystem.h`, `Reflection.hpp`, `Delegate.h`.

`Core.Mathf.h`(21KB)는 그 안에서 다시 assimp 3종과 nlohmann/json을 끌어온다. 즉
`Core.Minimal.h` 한 줄이 assimp와 json 전체를 모든 TU에 넣는다. 유니티 빌드에서는
이 비용이 한 번으로 상쇄되지만, **전이 include가 의존성을 감추는 문제**는 그대로다.
이건 `Phase4CouplingPlan.md`의 간선 절단과 같은 성격이라 그쪽 래칫 게이트와
함께 다뤄야 한다.

### 1.8 HashingString (2026-08-14 분석)

`Object::m_name` / `GameObject::m_tag` / `GameObject::m_layer` / `Scene::m_sceneName`의
타입이라 오브젝트 모델 전반에 걸려 있다. 외부 참조 21회 / 9파일.

설계 의도(문자열 비교 O(n) → 해시 비교 O(1))는 타당하고 해시 1회 계산도 의도대로
동작하지만, **이득을 회수하는 지점이 전부 값 복사로 막혀 있다.**

| # | 결함 | 근거 | 여파 |
|---|---|---|---|
| H-a | 인스펙터 이름 편집이 3중으로 깨짐 | `ReflectionImGuiHelper.h:538-540` | 아래 상술 |
| H-b | `string_view::data()`를 `const char*`로 전달 | `Scene.cpp:411`, `GameObject.h:44` | 널 종료 미보장 → UB, 길이 정보 소실 |
| H-c | `==`는 해시만, `<=>`는 해시+문자열 | `:72` vs `:65` | 표준 알고리즘의 ==/< 일관성 위반 |
| H-d | `GetHashedName()`·`ToString()` 값 반환 | `Object.h:40` 외 3곳 | 충돌 콜백 경로에서 할당 2회 |
| H-e | `std::hash<HashingString>` 특수화 없음 | — | 해시 컨테이너 키로 못 씀 |
| H-f | `Scene::GetGameObject`가 O(n) + 요소마다 복사 | `Scene.cpp:409-420` | `std::string` 선형 탐색보다 느릴 수 있음 |
| H-g | `data()`/`size()`가 non-const | `:97-98` | `const&`로는 `size()`조차 못 부름 |
| H-h | 빈 문자열 정책 4갈래 | 생성자·대입·기본생성·`SetString` | 즉사 / 통과 / 합법이 경로마다 다름 |

**H-a 상술** — `ReflectionImGuiHelper.h:538`:

```cpp
HashingString value = std::any_cast<HashingString>(prop.getter(instance));  // 복사본
if (ImGui::InputText(prop.name, value.data(), value.size() + 1))
```

1. 복사본을 편집하고 `prop.setter`를 부르지 않는다 → **편집이 반영되지 않는다.**
2. `data()`가 `char*`를 노출해 ImGui가 내부 버퍼에 직접 쓴다 → **`m_hash`가 갱신되지
   않아 문자열과 해시가 어긋난다.** 이 상태의 `==` 결과는 전부 신뢰할 수 없다.
3. 버퍼 크기로 `size() + 1`(= 현재 길이)을 넘긴다 → **이름을 늘릴 수 없고**,
   `capacity()`를 넘는 경우 범위 밖 쓰기가 된다.

근본 원인은 해시를 캐시하는 타입이 내부 버퍼의 쓰기 권한을 외부에 준 것이다.

**H-f 상술** — `Scene.cpp:411`:

```cpp
HashingString hashedName(name.data());
for (auto& obj : m_SceneObjects)
    if (obj && obj->GetHashedName() == hashedName)   // 요소마다 문자열 복사
```

`GetHashedName()`이 값 반환이라 **순회하는 오브젝트 수만큼 힙 할당**이 일어난다.
해시 하나를 비교하려고 문자열을 통째로 복사한다 — 이 클래스를 쓰는 목적과 정반대다.
`ConsoleCommandSystem.cpp`에서만 이 오버로드를 14곳 호출한다.

### 1.9 `plf_colony.h` — 배치가 잘못됐다 (2026-08-14 추가 실측)

214KB 단일 헤더가 `Core.Definition.h:78`에 들어 있다. `Core.Definition.h`는
`Core.Minimal.h:2`이므로, **이 헤더가 소스 1,185개 대부분에 파싱된다.**

그런데 실제 소비자는 **`AIManager.h:69-70` 두 멤버, 단 한 파일**이다.

**존폐 문제가 아니라 배치 문제다.** `Core.Definition.h`에서 빼고 `AIManager.h`가
직접 include하면 끝난다. 소비자가 하나뿐이라 위험도가 매우 낮다 → U4.

컨테이너 선택 자체는 두 사용처의 판정이 갈린다:

| 멤버 | 요건 | 판정 |
|---|---|---|
| `plf::colony<BlackBoard> m_blackBoards` | `m_blackBoardFind`가 `BlackBoard*`를 보관(`:68`) → **포인터 안정성 필수** | **적합** |
| `plf::colony<pair<weak_ptr<GameObject>, IAIComponent*>> m_aiComponentMap` | 조회 0회, 전부 선형 순회 | **부적합** |

**`m_blackBoards`는 올바른 선택이다.** `AIManager.cpp:16-18`이 `&(*it)`로 주소를 꺼내
맵에 넣으므로 `vector`(재할당)도 `deque`(중간 삭제)도 쓸 수 없다. colony가 정확히
이 용도다. 다만 이점을 절반만 쓴다 — `AIManager.cpp:28`이 하나를 지우려고 전체를
순회한다. `m_blackBoardFind`가 포인터 대신 `colony::iterator`를 들면 O(1)이 된다.

**`m_aiComponentMap`은 되돌리는 편이 낫다.** 주석이 이력을 남기고 있다 —
`AIManager.cpp:89`·`:99-103`에 `unordered_map` 시절의 `find`/`erase(it)`가 주석으로
있다. colony로 바꾸면서 O(1) 조회를 잃고 O(n) `erase_if`(`:105`)를 얻었는데,
**여기서 포인터 안정성은 요건이 아니다**(pair를 가리키는 외부 포인터가 없다).
순회가 주 작업이면 `vector`가 colony보다 빠르다 — colony는 삭제 슬롯을 건너뛰려
skipfield를 읽어 순회에 분기가 붙는다.

---

## 2. 트랙 구조

네 트랙은 **서로 독립적으로 가치가 있고**, 어디서 멈춰도 그 시점의 개선이 남는다.
다만 S0만은 순서 밖에서 먼저 처리한다.

| 트랙 | 내용 | 위험도 | 선행 |
|---|---|---|---|
| **S** (Standard) | 표준 회귀 — UB 제거, C++20 프리미티브 | S0 낮음 / S1~ 중간 | S0은 즉시 |
| **U** (Unused) | 죽은 코드·껍데기·문서 불일치 정리 | 낮음 | 없음 |
| **H** (HashingString) | 결함 수정 + 성능 회수 | H0~H2 낮음 / H3~ 중간 | 없음 |
| **C** (Coupling) | 우산 헤더 해체 | 높음 | Phase4 래칫과 협조 |

**권장 진행 순서: S0 → U0 → H0~H2 → U1 → H3~H4 → S1 → S2 → C.**
앞쪽 넷은 서로 겹치지 않아 병행 가능하다.

---

## 3. 단계별

### 트랙 S — 표준 회귀

#### S0. `namespace std` 침범 제거 · 순서 밖 최우선

`Core.Runtime.h`의 `std::null_exception`을 프로젝트 네임스페이스로 옮긴다.

- `Core::NullException`으로 옮기고 호출부 2곳(`Component.cpp:12`·`:20`)을 고친다.
- `Component.inl:9`·`:25`의 주석도 함께 정리한다 (같은 이름이 되살아나지 않게).
- 이때 예외 타입이 옳은지도 함께 본다 — "소유자 미설정"과 "컴포넌트 없음"에
  같은 예외를 쓰고 있고, 후자는 예외가 아니라 `nullptr` 반환이 맞을 수 있다.
  **다만 이 판단은 S0의 범위 밖이다.** 우선 네임스페이스만 옮긴다.

**게이트**: `namespace std` 안의 사용자 선언이 프로젝트 전체에서 0건. 빌드 통과.

#### S1. `Thread` → `std::jthread`

`Core.Thread.h`의 `Thread`(소비자 9파일)를 `std::jthread` + `std::stop_token`으로.
정지 플래그(`m_stopRequested`)와 `RequestStop`/`Join`이 `stop_token`과 의미가 같다.

**이때 함께 고칠 것** — 현재 `Start()`는 어설션 뒤에 복구가 없다. 릴리스에서는
어설션이 사라지므로 이미 실행 중인 스레드의 핸들을 덮어써 **핸들 누수 + 원래 스레드
미조인**이 된다. `jthread` 이식이 이 문제를 구조적으로 없앤다.

**게이트**: 소비자 9파일 빌드 통과. 종료 경로에서 스레드 누수 0
(`Tools/regression`의 셧다운 검사 통과).

#### S2. `CountingSemaphore` → `std::counting_semaphore`

소비자가 `Core.ThreadPool.h` 하나뿐이라 표면이 작다. S1 이후에 한다.

`Barrier`는 3-2G에서 소비자와 함께 은퇴했다. `Fence`는 §1.2대로 **사용처가 0**이므로
트랙 U에서 멤버 선언과 함께 지운다.

### 트랙 U — 죽은 코드 정리

#### U0. 소비자 0인 것 제거

- `FileIO.h` 삭제
- `DirectXHelper.h` 삭제
- `EngineSetting.h:129-130`의 `RenderCommandFence`·`RHICommandFence` 멤버 삭제 →
  참조가 0이 되면 `Core.Fence.h`도 삭제
- `.vcxproj` / `.filters` 항목, `Doc/Docs_Index.md` 목차 행 동반 정리

`Core.Assert.hpp` 제거(2026-08-14 완료)와 같은 절차다. 그때 확인된 함정을 반복하지
않는다 — **유니티 빌드에서는 헤더를 빼도 전이 include로 우연히 컴파일된다.**
파일을 지운 뒤 반드시 정식 빌드로 확인한다.

**게이트**: 삭제 대상 심볼의 전체 참조 0건. 전체 솔루션 빌드 통과.

#### U1. 껍데기·명명 정리

- `Core.Thread.hpp` — 이름이 내용과 어긋난다(스레드가 아니라 스레드 풀). 소비자를
  `Core.ThreadPool.h` 직접 include로 바꾸고 삭제
- `Reflection.hpp` — 같은 성격. 단 `Core.Minimal.h:9`가 쓰므로 U1은 트랙 C와 겹친다.
  C 이전에는 그대로 둔다
- `ReflectionMecro.h` → `ReflectionMacro.h` 철자 수정 (소비자 1)

#### U2. 문서-구현 불일치 해소

§1.5의 pak 압축·암호화. **셋 중 하나를 골라야 한다:**

1. 코덱을 구현한다 (lz4는 이미 vcpkg에 있다)
2. README·EngineStructureAnalysis에서 해당 주장을 내린다
3. 미구현임을 명시한다

배포 산출물의 성질이 걸린 문제이므로 `EnginePackagingPlan.md`와 함께 판단한다.
**이 항목은 결정이 필요하며, 코드 작업이 아니다.**

#### U4. `plf_colony.h` 배치 이동

`Core.Definition.h:78`의 include를 빼고 `AIManager.h`가 직접 include한다.
214KB 헤더가 전 TU에 파싱되던 것이 1개 TU로 줄어든다.

**주의**: 유니티 빌드에서는 다른 파일이 `Core.Definition.h` 경유로 이 헤더를
암묵 의존하고 있어도 드러나지 않는다. U0과 같은 함정이므로 이동 후 정식 빌드로
확인한다. `EngineStructureAnalysis.html:649`의 "메모리: … plf::colony" 서술도
실태(AI 전용)에 맞게 손본다.

**게이트**: `Core.Definition.h`에서 제거 후 전체 솔루션 빌드 통과.

#### U3. 소비자 1곳 유틸 개별 판정

§1.3의 6건. 각각 "인라인할 것 / 존치할 것"을 판정한다. 일괄 처리하지 않는다 —
`MemoryPool`(6.7KB)과 `RingBuffer`(1.4KB)는 성격이 다르다.

### 트랙 H — HashingString

#### H0. 인스펙터 편집 복구 (기능 파손)

`ReflectionImGuiHelper.h:538-540`. 고정 버퍼로 받아 편집하고, 변경 시 `prop.setter`를
호출하는 형태로 바꾼다. 다른 타입 브랜치가 이미 쓰는 패턴을 따른다.

**게이트**: 인스펙터에서 오브젝트 이름을 현재보다 길게 변경 → 저장 → 재로드 후 유지.

#### H1. `.data()` UB 제거

`Scene.cpp:411`, `GameObject.h:44`에서 `.data()`를 뺀다. `string_view` 오버로드가
이미 있으므로 그쪽이 선택되게 하는 것으로 충분하다.

**게이트**: 부분 뷰(`sv.substr(0, n)`)를 넘겨도 잘린 이름이 저장되는지 확인.

#### H2. `==`/`<=>` 일관성 + `data()` 봉인

- `operator==`가 `<=>`와 같은 기준(해시 → 문자열 tie-break)을 쓰게 한다. 해시가
  먼저 걸러주므로 비용은 사실상 그대로다
- `data()`의 `char*` 반환을 없앤다 (H0이 선행되어야 한다)
- `data()`/`size()`에 `const` 부여, 무의미한 `constexpr` 제거

#### H3. 값 반환 제거 (성능 회수)

`GetHashedName()` 3곳과 `ToString()`을 `const&` 반환으로. 호출부 대부분은 수정 없이
그대로 동작한다.

**게이트**: `GetHashedName().ToString()` 관용구에서 할당 0회.
충돌 콜백 경로(`EntityEnemy.cpp:76·117`)에서 프레임당 할당 감소를 프로파일러로 확인.

#### H4. `std::hash` 특수화 + 조회 경로

- `GetHash()` 접근자 + `std::hash<HashingString>` 특수화 추가 (신규라 안전)
- `Scene.h:367`의 `CanvasMap` 키를 `std::string` → `HashingString`으로 전환 검토

**`Scene::GetGameObject`의 O(n) 선형 탐색은 이 트랙에서 다루지 않는다.**
`SceneGraphRedesignPlan.md` 트랙 E(E1·E3)가 `GetGameObject` 호출 60곳/14파일을 이미
마이그레이션 표면으로 잡고 있어, 여기서 따로 손대면 충돌한다. **H4는 그쪽이
쓸 수 있는 재료(해시 특수화)를 준비하는 데까지만 한다.**

#### H5. 빈 문자열 정책 통일 — 결정 필요

현재 4갈래(§1.8 H-h). 정할 것은 하나다: **빈 이름은 합법인가?**

- 합법이면 → 어설션을 전부 걷고 기본 생성자와 일관되게
- 불법이면 → 기본 생성자를 없애고 `SetString`에도 같은 검사를

코드 작업 이전에 답이 필요한 항목이다.

### 트랙 C — 우산 헤더 해체

`Core.Minimal.h`의 9개 전이를 해체한다. 위험도가 가장 높고 표면이 가장 넓다.
`Phase4CouplingPlan.md`의 간선 래칫 게이트와 **반드시 협조해야 한다** — 독립 진행하면
그쪽 카운트가 흔들린다. 앞의 세 트랙이 끝난 뒤 착수한다.

---

## 4. 게이트 관례

`BuildPipelinePlan.md`의 관례를 승계한다.

- 각 단계는 **전체 솔루션 빌드 통과**를 최소 게이트로 한다. 유니티 빌드라
  부분 빌드로는 헤더 삭제의 영향이 드러나지 않는다.
- 삭제 작업은 **삭제 → 참조 0건 재확인 → 빌드** 순서를 지킨다.
- 회귀 세트는 `pwsh`로 실행한다 (5.1은 한글 주석 인코딩이 깨져 거짓 실패가 난다).
- 커밋 전 `HEAD`를 재확인한다 — 같은 워크트리에서 동시 커밋이 일어난 전례가 있다.

## 5. 이 계획이 다루지 않는 것

- **`plf_colony.h`의 존폐** — 컨테이너 자체는 정당하다(§1.9). U4는 배치만 옮긴다.
- **`m_aiComponentMap`의 컨테이너 교체** — §1.9의 판정은 AI 시스템 소관이며,
  유틸리티 정리 계획이 단독으로 결정할 사안이 아니다. 근거만 남긴다.
- **`Core.Mathf.h`의 assimp/json 의존** — 트랙 C의 일부지만, 단독으로는
  `Core.Definition.h` 정리와 묶여야 해서 여기서 분리하지 않는다.
- **`Scene::GetGameObject`의 자료구조 교체** — `SceneGraphRedesignPlan.md` 트랙 E 소관.
- **`Singleton`·`SpinLock`의 설계 평가** — 둘 다 광범위하게 살아 있고(56회/24파일,
  55회/5파일) 표준 대응물이 없다. 별도 판단이 필요하며 "정리"의 범위가 아니다.

## 6. 진행 상태

| 단계 | 상태 | 비고 |
|---|---|---|
| — | ✅ | `Core.Assert.hpp` 제거 (2026-08-14). 표준 `assert` 통일, 잔여 참조 0. **빌드 미검증** |
| S0 | ⬜ | `namespace std` 침범 |
| S1 | ⬜ | `Thread` → `jthread` |
| S2 | ⬜ | `CountingSemaphore` |
| U0 | ⬜ | 죽은 코드 4건 |
| U1 | ⬜ | 껍데기 헤더 |
| U2 | ⬜ | pak 문서 불일치 — **결정 필요** |
| U3 | ⬜ | 소비자 1곳 6건 개별 판정 |
| U4 | ⬜ | `plf_colony.h` 배치 이동 (214KB, 전 TU → 1 TU) |
| H0 | ⬜ | 인스펙터 편집 복구 |
| H1 | ⬜ | `.data()` UB |
| H2 | ⬜ | `==`/`<=>` + `data()` 봉인 |
| H3 | ⬜ | 값 반환 제거 |
| H4 | ⬜ | `std::hash` 특수화 |
| H5 | ⬜ | 빈 문자열 정책 — **결정 필요** |
| C | ⬜ | 우산 헤더 — Phase4 협조 |
