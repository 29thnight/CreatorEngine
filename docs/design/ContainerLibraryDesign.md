# ce 컨테이너 라이브러리 설계 — `ce::dynamic_array`

2026-08-17. `UtilityFrameworkModernizationPlan`의 컨테이너 축을 구체화한다.
직접 선행은 **InlineVector 폐기**(`SceneGraphRedesignPlan` K2 스테이지 B) —
그 폐기가 남긴 판정 기준을 이 문서가 이어받는다.

핵심 입장: **STL이 기본값이고, 자체 컨테이너는 측정된 축에서만 만든다.**
"상용 엔진이 자체 컨테이너를 쓰니까"는 근거가 아니다.

---

## 1. 왜 자체 컨테이너인가 — 그리고 왜 대부분은 아닌가

### 1.1 상용 엔진 대조

Unreal(`TArray`/`TMap`/`TSet`), Godot(`Vector`/`HashMap`/`LocalVector`),
Unity C++ 코어(`dynamic_array`) 모두 STL을 쓰지 않는다. 그러나 **그들이 버린
이유가 이 엔진에 그대로 적용되지는 않는다.**

| 그들이 STL을 버린 이유 | 이 엔진에서의 무게 |
|---|---|
| 할당자가 타입 매개변수라 타입 정체성이 바뀐다 | **큼** — `ce::vector<T>`가 `is_vector_v`에 안 걸려 리플렉션 필드가 유실되는 것이 실측됐다 |
| 디버그 빌드 성능(MSVC `_ITERATOR_DEBUG_LEVEL=2`) | **큼** — 실질 이득이 가장 큰 축 |
| `unordered_map`이 규격상 노드 기반이라 오픈 어드레싱 불가 | **큼** — 규격이 막고 있어 우회 불가 |
| 컴파일 시간·헤더 부하 | 중간 |
| 크기(`vector` 24B → 16B) | 중간 |
| DLL 경계 ABI | **작음** — `/MD` 공유 CRT |
| 구현체 간 결정성 | **작음** — MSVC 단일 |
| 콘솔·플랫폼 지원 | **없음** — Windows 전용 |

유효한 축은 **할당자 · 디버그 성능 · 해시맵 레이아웃** 셋뿐이다.

### 1.2 STL을 기본값으로 두는 이유 (실측)

이 저장소에서 **버그와 죽은 코드는 전부 자체 제작물에서 나왔다**:

- `HashingString::operator==`가 해시만 비교 — `operator<=>`(문자열까지 비교)와
  서로 모순되는 **정확성 결함**
- `MyAllocator::allocate` 정렬 인자 무시 — 과다 정렬 타입에서 조용한 미정렬
- 프로파일러 `LinearAllocator` 동일 결함
- `MemoryPool` · `ce::` 컨테이너 alias 20여 개 · `Core.Fence` — 소비자 0으로 사망
- `InlineVector` — 측정 패리티 + 오측 근거로 1일 만에 폐기

반면 `std::vector`는 1,030곳에서 쓰이는데 **버그의 원인이었던 적이 없다.**
자체 컨테이너는 영구 유지보수 부채다 — 예외 안전성, 정렬, 이터레이터 무효화
규칙, 리플렉션 연동, natvis 시각화, 그리고 만든 사람이 떠난 뒤의 비용까지.

---

## 2. 설계 결정

### 2.1 이름·시맨틱

```cpp
namespace ce { template<class T> class dynamic_array; }
```

- **STL 명명·시맨틱 유지**: `begin/end/size/capacity/empty/reserve/resize/
  push_back/emplace_back/pop_back/erase/insert/clear/data/front/back/operator[]`
- 이유는 성능이 아니라 **치환 비용**이다. `InlineVector`가 `std::vector`
  인터페이스 부분집합이었기 때문에 폐기 비용이 타입 별칭 한 줄이었다. 모양이
  달랐으면 되돌릴 수도 없었다. 이 교훈을 계약으로 박는다.

### 2.2 채택 기능

| # | 기능 | 근거 |
|---|---|---|
| 1 | **mimalloc 직결**(`MyAlloc`/`MyFree`), 할당자 템플릿 매개변수 없음 | 타입이 하나만 늘어난다. `std::vector<T,Alloc>`는 할당자마다 타입이 갈라져 리플렉션 특수화가 배로 는다 |
| 2 | **`mi_good_size` 용량 반올림** | mimalloc이 이미 반올림해 준 여유분을 `std::vector`는 영영 모른 채 버린다. `std::allocator`에 "얼마나 줄 건데?"를 묻는 창구가 없어 **구조적으로 못 얻는** 이득. 크기는 한 자릿수 % (빈 간격 ≈1.19배) |
| 3 | **`remove_at_swap`** (O(1) 비순서 삭제) | `SystemSchedule`이 이미 손으로 구현해 쓴다. Unreal `RemoveAtSwap`과 같은 이유 |
| 4 | **`resize_uninitialized` / `append_uninitialized`** | POD 버퍼의 값 초기화 생략. Godot `LocalVector`의 실질 이득이 realloc보다 이쪽 |
| 5 | **축소 금지를 타입이 보장** — `clear()`가 용량을 절대 안 버림 | ClrHost 큐의 용량 손실이 규약으로 관리되다 셋 중 셋이 어긋났다. 타입이 지키면 재발 불가 |
| 6 | **32비트 size/capacity** | `sizeof` 24 → 16B. GameObject마다 벡터를 지는 구조라 수천 개에서 유의미 |
| 7 | **명시적 성장 정책**(배율·고정 증분·상한) | 필드별로 고를 수 있게 |
| 8 | **정렬 처리** (`alignof(T)` > 기본 new 정렬이면 정렬 할당) | `MyAllocator`·`LinearAllocator`가 **둘 다 놓친** 결함. 새로 만들면서 반복하지 않는다 |
| 9 | **성장 계측 훅**(성장 횟수·피크 크기·최종 용량) | §5 C0의 도구. `reserve` 상수의 근거를 만든다 |

### 2.3 기각 기능

**realloc / `mi_expand` — 기각.**
1. mimalloc은 빈 기반이고 `mi_expand`는 요청 크기가 **현재 블록의 usable size
   안**에 들어올 때만 성공한다. 빈 간격(≈1.19배)보다 성장 배율(1.5~2배)이 크므로
   **정의상 항상 실패**한다.
2. realloc이 빛나는 경로는 대형 블록의 페이지 재매핑(Linux `mremap`)인데,
   **Windows + mimalloc 조합에는 그 경로가 없다.** 큰 배열도 복사한다.
3. 비트 단위 이동이므로 **trivially relocatable** 판정이 필요한데 표준 트레이트가
   없다(P1144 제안 단계). 손으로 유지하는 목록은 오판 시 **조용한 메모리 손상**을
   낸다 — 이 저장소가 이미 겪은 결함들 중 디버깅 난이도가 가장 높은 종류.

★ 같은 여유분은 `mi_good_size`(§2.2-2)가 **사전에, 실패 분기 없이** 챙긴다.
`mi_expand`와 `mi_good_size`는 같은 slack을 노리는 두 방식이고 후자가 낫다.

재검토 시점은 명확하다 — **파티클·탄막처럼 프레임마다 수만 원소가 오가는 대형
POD 배열이 생길 때.** 그때는 `reserve` 규율로 안 되는 구간이 생기고 원소가 확실한
POD라 트레이트 위험도 사라진다.

**SBO — 기각(승계).** `SceneGraphRedesignPlan` K2 스테이지 B 폐기 결정을 잇는다.
재적용은 넷을 **모두** 충족할 때만: ① 제거되는 힙 할당이 핫패스에서 실제 지불되고
② 인라인 버퍼가 비는 비율 × 크기 증가분이 그 이득보다 작고 ③ 객체가 커져서 손해
보는 다른 순회가 없고 ④ before/after 측정치가 있다.

**할당자 템플릿 매개변수(`ce::vector<T,Alloc>`) — 기각.** §2.2-1 참조.

---

## 3. 필수 선행 조건 — 리플렉션 특수화

`is_vector_v` / `VectorElementType` 짝을 **먼저** 넣지 않으면
`[[Property]] ce::dynamic_array<T>` 필드는:

- 저장: `node[name] = "[not support type]"` — **경고 없이 문자열이 들어간다**
  (`ReflectionTypedYml.h:399`)
- 로드: 에러 로그만 남고 값은 복원되지 않음 (`ReflectionTypedYml.h:533`)

즉 **빌드는 통과하고 저장도 성공한 것처럼 보이는데 왕복하면 데이터가 사라진다.**
컨테이너 코드보다 이 특수화가 먼저다.

★ 부수 작업으로 이 `else` 분기를 `static_assert` 또는 저장 시점 에러로 승격할
것을 권한다 — 지금은 저장 쪽이 완전히 조용해서 **이미 새고 있는 필드가 있는지조차
알 수 없다.**

---

## 4. 첫 소비자 — 만들기 전에 정한다

이 저장소에서 자료구조는 만들어서가 아니라 **정본으로 지정되지 않아서** 죽는다
(§1.2의 사망 목록). 구 RHI가 "소비자 없는 추상"으로 죽은 것과 같은 사인이다.

| 후보 | 이미 필요로 하는 기능 | 판정 |
|---|---|---|
| `SystemSchedule` 6개 리스트 | `remove_at_swap`을 **이미 손으로 구현 중** | **채택** — 기능 요구가 코드에 이미 있다 |
| ClrHost 이벤트 큐 4종 | 축소 금지 정책 | 보류 — 상위 해법이 `BatchMailbox`(별도 정본)라 그쪽이 먼저 |
| `Scene` Collect* 캐시 8종 | 프레임당 복사 제거 | 보류 — `FrameArena` 소관 |

---

## 5. 슬라이스

**C0 — 성장 계측 (0.5일)**
`std::vector` 위에 계측을 얹어 주요 컨테이너의 **성장 횟수 · 실제 피크 크기 ·
최종 용량**을 잰다. 산출: 각 `reserve` 상수를 측정값으로 교체하거나 삭제.
★ 이 저장소의 `reserve` 값들(`Scene` 3000 · `TagManager` 300/32 ·
`Meta::Registry` 128)은 **근거가 확인되지 않은 매직 넘버**다 —
저작된 최대 씬이 68 오브젝트인데 3000을 예약한다. 이 계측이 그것을 정리한다.
판정: 상수 전부가 측정 근거를 갖거나 삭제됨.

**C1 — 리플렉션 특수화 선행 (0.5일)**
§3. `is_vector_v`/`VectorElementType`에 `ce::dynamic_array` 짝 추가 +
미지원 타입 `else` 분기를 시끄럽게. 판정: 씬·프리팹 전수에서
`"[not support type]"` 0건(현재 유출 여부도 이때 밝혀진다).

**C2 — `ce::dynamic_array` 구현 (2일)**
§2.2의 기능 1~9. 단위 테스트: 정렬 타입(`alignas(64)`), 무브 온리 타입,
예외 던지는 타입의 강한 보장, `remove_at_swap` 순서 비보존, `clear()` 후 용량
유지, `mi_good_size` 반올림이 실제 usable size와 일치.
판정: 테스트 전수 통과 + `sizeof(ce::dynamic_array<int>) == 16`.

**C3 — 첫 소비자 이관 (1일)**
`SystemSchedule` 6개 리스트를 이관하고 손으로 구현한 swap-and-pop을
`remove_at_swap`으로 대체. 판정: 회귀 세트 통과 + 프리팹 왕복 검사 통과 +
C0 계측에서 성장 횟수 비퇴행.

**C4 — 판정과 확대 결정 (0.5일)**
C0 대비 수치로 확대 여부를 정한다. **이득이 없으면 C3에서 멈추고 되돌린다** —
`InlineVector`가 준 교훈이다(측정 패리티인데 유지하지 않는다).

합계 **4.5일**. 순서 제약: C0·C1 → C2 → C3 → C4 (C0과 C1은 병행 가능).

---

## 6. 완료 기준

1. `reserve` 상수 전부가 측정 근거를 갖거나 삭제됐다.
2. 씬·프리팹 전수에 `"[not support type]"` 0건이고, 미지원 타입은 선언
   시점에 걸린다.
3. `sizeof(ce::dynamic_array<int>) == 16`, 정렬·무브온리·예외 테스트 통과.
4. `SystemSchedule`에 손으로 구현한 swap-and-pop이 0건.
5. C0 대비 성장 횟수·할당 바이트가 비퇴행이고, **개선이 수치로 있다**
   (없으면 §5 C4대로 되돌린다).
6. 회귀 세트 · 프리팹 왕복 검사 착수 전과 동일 판정.

## 7. 하지 않을 것

- **`std::vector` 전면 치환** — 1,030곳을 건드릴 이유가 없다. 측정된 축에서만.
- **realloc / `mi_expand`** — §2.3.
- **SBO 재도입** — §2.3의 4조건 전이면 안 된다.
- **`ce::` 컨테이너 alias 20여 개 부활** — 죽은 이유가 소비자 부재다.
  `ce::dynamic_array`가 자리를 잡은 뒤에도 나머지는 필요할 때만 하나씩.
- **`std::string` 대체** — 별개 축(`Symbol` 인터닝)이 먼저다.

## 8. 리스크

- **또 하나의 죽은 컨테이너가 될 위험** — 이 저장소의 대표 실패 양식이다.
  §4가 소비자를 먼저 못 박고 §5 C4가 되돌림 조건을 명시하는 이유.
- **리플렉션 특수화 누락 시 조용한 데이터 유실** — C1을 C2보다 앞에 둔 이유.
  순서를 바꾸면 안 된다.
- **예외 안전성** — `InlineVector` 리뷰가 남긴 미검증 경로와 같은 함정.
  C2 테스트에 "던지는 T"를 반드시 포함한다.
- **디버그 빌드 이득은 이 컨테이너로 다 오지 않는다** — 가장 큰 축(§1.1)은
  빌드 구성(`_ITERATOR_DEBUG_LEVEL`) 문제이고, 그건 별도 결정이다.
  이 문서의 이득을 그것으로 부풀리지 않는다.

## 9. 관련 문서

- [UtilityFrameworkModernizationPlan.md](../plans/UtilityFrameworkModernizationPlan.md) — 상위 계획
- [SceneGraphRedesignPlan.md](../plans/SceneGraphRedesignPlan.md) — K2 스테이지 B 폐기 결정, SBO 재적용 기준
- [ReflectionRedesignPlan.md](../plans/ReflectionRedesignPlan.md) — 리플렉션 표면
- [PPLContainerMigrationAnalysis.md](../analysis/PPLContainerMigrationAnalysis.md) — PPL 컨테이너 축
