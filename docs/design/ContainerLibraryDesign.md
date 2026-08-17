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

### 2.4 ★ 이 중 새 타입이 정말 필요한 것은 절반이다

C0를 다시 쓰면서 드러난 것 — 위 목록은 성질이 다른 셋을 한 묶음으로 적고 있었다.
**새 타입을 만들어야만 얻는 것**과 그렇지 않은 것을 갈라 두지 않으면, 이득을
`ce::dynamic_array`의 공으로 부풀리게 된다.

| 층 | 항목 | 비고 |
|---|---|---|
| **새 타입이 필요** | mimalloc 직결 · `mi_good_size` · 32비트 size/capacity · 축소 금지를 타입이 보장 · 성장 정책 · 정렬 처리 | 전부 타입의 내부 표현이나 할당 경로를 바꾸는 것 |
| **`std::vector` 위 자유 함수면 충분** | `remove_at_swap` | `ce::remove_at_swap(vec, i)` 한 줄. 타입을 안 바꾸니 리플렉션도 안 건드린다. **트랙 K가 접혀도 이건 할 수 있다** |
| **빌드 플래그 문제** | 디버그 이터레이터 검사 비용 | 컨테이너와 무관. C0-2가 잰다 |
| **새 타입 + UB 인접** | `resize_uninitialized` | 소비자(대형 POD 배열)가 아직 없어 C2 이후에나 판단 가능 |

즉 §1.1이 꼽은 유효한 축 셋 중 **디버그 성능은 컨테이너로 오지 않는다.**
이 문서의 이득을 그것으로 부풀리지 않는다(§8과 같은 취지).

**성장 계측 훅**은 초안의 기능 9번이었으나 C0 폐기와 함께 뺐다 — 잴 대상이
70KB였다.

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

**C0 — go/no-go 프로브 (0.5일)**

★ **초안의 C0("성장 계측으로 `reserve` 상수 정리")는 폐기했다.** 전수를 뽑아
크기를 계산해 보니 문제 삼은 상수의 총합이 **약 70KB**였다 — `Scene` 3000이
60KB(shared_ptr 16B + uint32 4B), `TagManager` 6개가 8KB, `Meta::Registry`가 2KB.
`RenderPassData`의 500 둘은 아예 죽은 코드다(`PushRenderQueue` 호출자 0).
70KB를 재려고 계측 하네스를 세울 수는 없다. 게다가 `Meta::Registry` 128은
등록 타입 수라 **재는 게 아니라 세면 되고**, `TagManager`는 `TagManager.asset`의
태그 수라 역시 셀 수 있다. → §7의 잡무 항목으로 격하.

대신 **트랙 전체의 go/no-go**를 묻는 프로브 둘을 돌린다. 둘 다 계측 코드를
새로 짜지 않는다.

1. **mimalloc 내장 통계** — `mi_stats_print`로 할당 총량과 사이즈 클래스 분포를
   본다. "할당량이 애초에 유의미한가"에 답한다. (vcpkg mimalloc이 `MI_STAT`을
   켜고 빌드됐는지 확인 선행.)
2. **`_ITERATOR_DEBUG_LEVEL=0` 빌드 시도** — 현재 어디에도 설정이 없어 Debug
   (`/MDd`)는 MSVC 기본값 **2**(전면 이터레이터 검사)로 돈다. §1.1에서 "실질
   이득이 가장 큰 축"으로 꼽은 것이 이건데 **컨테이너를 하나도 안 만들고**
   얻을 수 있다. 이 매크로는 링크 단위 ABI라 vcpkg 디버그 라이브러리와 값이
   맞아야 하고, 안 맞으면 링커가 거부한다 — **그 난이도 자체가 측정 결과다.**

판정: 1의 할당량이 미미하고 2가 큰 이득을 주면, `ce::dynamic_array`의 정당성은
"디버그 빌드 + `remove_at_swap`"으로 좁아진다. 그런데 **둘 다 새 타입이 필요
없다**(§2.4) — 그 경우 트랙 K를 접는다. §5 C4의 "이득이 없으면 되돌린다"를
착수 지점으로 당긴 것이다.

#### C0 실행 결과 (2026-08-17)

**프로브 2 — 완료. `_ITERATOR_DEBUG_LEVEL=0`은 플래그 하나로 안 된다.**
저장소를 건드리지 않고 `/p:ForceImportBeforeCppTargets`로 정의만 주입해 전체
솔루션을 빌드했다. **첫 프로젝트에서 즉시 `LNK2038`**:

```
mimalloc-static-debug.lib(alloc.c.obj) : error LNK2038:
  '_ITERATOR_DEBUG_LEVEL'에 대해 불일치가 검색되었습니다.
  '2' 값이 '0'(MemoryManager.obj에 위치) 값과 일치하지 않습니다.
  [ManagedHeap.vcxproj]
```

vcpkg 디버그 라이브러리가 전부 기본값 2로 빌드돼 있어, 엔진만 0으로 돌리면
링크가 막힌다. 즉 이 축을 열려면 **커스텀 트리플릿을 만들어 34개 포트를 전부
재빌드**해야 한다. 실험 후 `/t:Rebuild`로 정상 상태를 복구했다(양 실행 파일 링크 확인).

★ 이것이 트랙 K의 계산을 바꾼다. §1.1이 "실질 이득이 가장 큰 축"으로 꼽은
디버그 빌드 성능은 ① 이 컨테이너와 **무관**하고(§2.4) ② **vcpkg 전면 재빌드라는
별도 프로젝트**다. 그러니 이 축의 이득을 `ce::dynamic_array`의 정당성으로 쓸 수
없다. 이 축은 빌드 인프라 트랙으로 분리해 따로 결정한다.

**프로브 1 — 계측기를 두 번 고쳐서 답을 얻었다. 결론은 GO.**

계측기 ①(`mi_stats_print`)은 **틀렸다.** 전역 `operator new` 오버라이드가 저장소에
없어(실측: 클래스 스코프 오버로드가 `ManagedHeapObject.h:38-52` 한 곳뿐)
`std::vector`의 버퍼는 **CRT 힙**으로 간다. mimalloc을 지나는 것은
`Managed::HeapObject` 파생뿐이다. 즉 그것은 **옮기려는 바로 그 할당을 못 본다.**

계측기 ②(`_CrtMemCheckpoint`)도 **혼자서는 틀린 답을 준다.** 그것은 그 순간 살아
있는 블록만 세므로 "프레임마다 할당했다 그 프레임에 해제하는" 패턴이 **0으로
보인다** — 그런데 그것이 정확히 이 컨테이너가 없애려는 비용이다. 실제로 순증만
봤을 때 재생 300프레임 동안 CRT +517블록이 나와 **"프레임당 할당이 거의 없다"는
정반대 결론에 도달할 뻔했다.**

계측기 ③ — `_CrtSetAllocHook`으로 **할당 호출 자체를 센다**(`_CRT_BLOCK` 제외).
이것이 churn을 본다. 셋을 합쳐 `mem.stats`/`mem.delta`/`mem.reset`/`mem.hook`
콘솔 명령으로 넣었다(`ConsoleCommandSystem.cpp`, ManagedHeap에는
`MyHeapStats`/`MyHeapStatsReset` 추가).

#### 실측 (Test1 씬 68오브젝트, Debug x64, `--script` 무인 실행)

| 구간 | mimalloc | CRT live 블록 | **CRT 할당 호출(churn)** |
|---|---|---|---|
| 씬 로드 | +213건 / 0.13MB | +8,740 | — |
| 편집 180프레임 | 0 | +436 | — |
| 재생 진입(1회성) | 0 | +95,756 / 3.33MB | — |
| **재생 300프레임** | **0** | +517 | **459,964건 / 41.54MB** |
| **재생 600프레임** | **0** | −412 | **915,603건 / 82.51MB** |

**프레임당 CRT 할당 약 1,530건 / 137KB.** 300↔600프레임이 정확히 2배라 선형이
확인됐고, live 블록은 거의 안 늘어난다 — 즉 **99.9%가 프레임 내에서 할당했다
해제되는 churn이다.** mimalloc 쪽은 런타임 내내 **0건**이다(씬 로드 때 213건뿐).

★ 재생 진입 시 +95,756블록은 누수가 아니다. 구간을 셋으로 나눠 재보니 +330 → +3
→ +89로 평탄해졌다 — `CreateEditorOnlyPlayScene`의 1회성 복제다.

#### 귀속 (2026-08-17, `mem.hook stack` + `mem.hook top`)

할당 훅에 `CaptureStackBackTrace`를 붙여 호출 지점별로 모았다(고정 표 1024칸,
심볼 해석은 훅을 끈 뒤). 120프레임 재생에서 **서로 다른 호출 지점 1024개 이상
(표 넘침) / 131,250건**. 최상위도 2.1%뿐인 **긴 꼬리**다 — 단일 핫스팟이 없다.

★ **그런데 상위 목록이 판정을 뒤집는다.** 1·2·3·4·5·6·8위가 전부
`std::allocator<std::_Container_proxy>::allocate`다. `_Container_proxy`는
**`_ITERATOR_DEBUG_LEVEL ≥ 1`일 때 MSVC가 컨테이너 인스턴스마다 추가로 하는
힙 할당**이다. 즉 상위 할당의 상당 부분이 **디버그 빌드에만 존재하는 계측
부산물**이고, Release(IDL=0)에는 아예 없다.

실제 코드에서 온 것으로 확인된 것:

| 출처 | 성격 |
|---|---|
| `DX12GpuProfiler::PassTiming` 생성자의 `std::string` | 프레임마다 패스 이름 문자열 — 진짜 비용 |
| `std::vector<RHITransition>::_Emplace_reallocate` | 렌더 그래프 전이 배열의 성장 — `reserve`로 없앨 수 있다 |
| `std::string` 각종 구축(`operator+`, `_Construct`) | 문자열 churn — `Symbol` 인터닝 축과 겹친다 |

#### 판정: **보류** — Debug 수치가 이터레이터 디버깅으로 오염됐다

앞 절의 "프레임당 1,530건"은 **Debug 빌드 수치이고, 그중 상당 부분이
`_Container_proxy`다.** Release의 실제 할당량은 이보다 크게 낮다. 그런데
CRT 디버그 힙 훅은 Release에 없으므로 **같은 도구로 Release를 잴 수 없다.**

즉 C0는 "GO"가 아니라 **"측정 방법을 하나 더 갖추기 전에는 판정 불가"** 다.
길은 둘이다:

1. **C0-2를 먼저 푼다** — 커스텀 트리플릿으로 vcpkg를 IDL=0으로 재빌드하면
   `_Container_proxy` 잡음이 사라져 같은 훅으로 깨끗한 수치를 얻는다.
   **성능 개선과 측정 정합이 같은 작업으로 해결된다** — 이것이 C0-2의 우선순위를
   올린다.
2. 또는 Release에서 동작하는 별도 계측(전역 `operator new` 교체 등)을 세운다.
   그런데 그건 §2.3에서 다룬 그 결정 자체라 순환이다.

**→ 1번을 먼저 한다.** 트랙 K의 착수 조건을 "C0-2 해결 후 재측정"으로 바꾼다.

★ 다만 귀속에서 나온 **개별 항목 둘은 트랙 K와 무관하게 지금 고칠 수 있다**:
`DX12GpuProfiler::PassTiming`의 프레임당 문자열 생성, `vector<RHITransition>`의
`reserve` 누락. 둘 다 컨테이너 라이브러리가 아니라 호출부 수정이다.

#### (참고) 오염 이전의 원 판정

- `ce::dynamic_array`가 겨냥하는 CRT 힙이 **런타임 할당의 사실상 전부**를 쥐고 있다.
  mimalloc 쪽은 이미 0이라 더 짜낼 것이 없다.
- 60fps 기준 **초당 약 92,000건**이다. 무시할 수 있는 양이 아니다.
- 다만 이 1,530건이 전부 `std::vector`는 아니다 — `std::string`·`std::function`·
  yaml-cpp 노드도 같은 힙에 있다. **다음 질문은 "이 churn의 출처"이고,
  그것이 C2의 첫 소비자를 실제 데이터로 다시 고르게 한다**(§4의 `SystemSchedule`
  선정은 코드 근거였지 측정 근거가 아니었다).
- ★ 그 자체로 별개 가치가 있는 발견이다. **프레임당 1,530건은 컨테이너 문제이기
  이전에 "프레임 루프가 왜 이렇게 할당하는가"라는 질문**이고, 앞서 실측된
  ClrHost 3개 큐의 swap 재할당 · `Scene::UpdateRenderData`의 벡터 8개 복사 ·
  `ProxyCommand`의 스킨드 메시당 32KB `make_shared`가 모두 후보다.

**C1 — 리플렉션 미지원 타입을 컴파일 오류로 (2026-08-17 완료)**
`ce::dynamic_array` 특수화는 C2에서 타입이 생긴 뒤에 붙인다. C1에서 실행한 것은
**미지원 타입의 조용한 유실을 없애는 것**이고, 그 과정에서 실제 유실 2종이 나왔다.

발견 ① **`Skeleton::m_rootTransform`이 저장마다 유실되고 있었다.**
`Mathf::xMatrix`(=`XMMATRIX`)가 스칼라 목록에 없어 `"[not support type]"` 문자열로
덮여 왔다 — `Test1.creator`·`Test2.creator`에 그 흔적이 남아 있다(디스크 전수
851파일 중 2건). `EmitScalar`/`ReadScalar` 짝을 추가해 행 우선 16 float 시퀀스로
왕복하게 했다(SIMD 레지스터 표현이 아니라 `XMFLOAT4X4`로 내려 적어 정렬·플랫폼
의존을 없앤다). 낡은 파일의 문자열은 로드 시 항등행렬로 떨어지고 다음 저장에서
정상 값으로 교체된다.

발견 ② **`MeshColliderComponent::m_Info`는 한 번도 왕복한 적이 없었다.**
`ConvexMeshColliderInfo`는 원시 포인터(`Vector3* vertices`)를 담은 런타임 구조체다.
★ 이 건은 **디스크 증거로는 잡을 수 없었다** — 이 컴포넌트가 저장된 씬·프리팹이
0건이라 `"[not support type]"` 문자열조차 남지 않았고, `static_assert` 승격 후
빌드에서만 드러났다. **컴파일 타임 검사가 디스크 grep의 상위집합**임을 보여 준다.
관례(`BoxColliderComponent`는 정보 구조체가 아니라 컴포넌트의 미러 필드를 저장)를
따라 필드 목록에서 뺐다.

변경: `EmitMember`/`ReadMember`의 `else`를 `static_assert(kUnsupportedForYaml<T>)`로
승격(의존 거짓 관용구). 오류 메시지가 세 가지 처방을 직접 지시한다 — 스칼라 짝
추가 / `reflect()` 부착 / 필드 목록에서 제외.

판정: **Debug x64 전체 솔루션 빌드 통과** = 모든 `[[Property]]` 필드가 직렬화
가능함이 컴파일 타임에 보장된다. 이제 미지원 타입은 저장 시점이 아니라 **선언
시점**에 걸린다.

**C2 — `ce::dynamic_array` 구현 (2일) — C0 통과 조건부**
§2.4의 "새 타입이 필요" 여섯. 단위 테스트: 정렬 타입(`alignas(64)`), 무브 온리
타입, 예외 던지는 타입의 강한 보장, `clear()` 후 용량 유지, `mi_good_size`
반올림이 실제 usable size와 일치. 리플렉션 특수화(`is_vector_v`/
`VectorElementType`)를 이 슬라이스에서 함께 붙인다 — C1이 만든
`static_assert`가 누락을 바로 잡아 준다.
판정: 테스트 전수 통과 + `sizeof(ce::dynamic_array<int>) == 16`.

**C3 — 첫 소비자 이관 (1일)**
`SystemSchedule` 6개 리스트를 이관한다. 판정: 회귀 세트 통과 + 프리팹 왕복 검사
통과 + C0-1 대비 할당 총량 비퇴행.

**C4 — 판정과 확대 결정 (0.5일)**
C0 기준선 대비 수치로 확대 여부를 정한다. **이득이 없으면 C3에서 멈추고
되돌린다** — `InlineVector`가 준 교훈이다(측정 패리티인데 유지하지 않는다).

**독립 항목 — `ce::remove_at_swap` (0.5일, 트랙 K와 무관하게 가능)**
`std::vector` 위의 자유 함수(§2.4). `SystemSchedule`이 손으로 구현한
swap-and-pop을 대체한다. 타입을 안 바꾸니 리플렉션·직렬화에 영향이 없고,
**C0에서 트랙 K를 접어도 이건 남는다.**

합계 **4.5일**(C0 0.5 · C2 2 · C3 1 · C4 0.5 · remove_at_swap 0.5. C1은 완료).
순서 제약: **C0가 관문이다** — 여기서 no-go면 C2~C4를 하지 않는다.
`remove_at_swap`은 어느 시점이든 독립적으로 가능.

---

## 6. 완료 기준

1. **C0 프로브 둘의 결과가 문서에 수치로 적혀 있다** — 그 수치가 트랙 K를
   진행할지 접을지의 근거다. no-go로 접는 것도 완료다.
2. ✅ 미지원 타입이 선언 시점에 걸린다(C1 완료). 씬·프리팹의 잔여
   `"[not support type]"` 2건은 다음 저장에서 자동 교체된다.
3. `SystemSchedule`에 손으로 구현한 swap-and-pop이 0건(`ce::remove_at_swap`).
4. **(C0 go인 경우만)** `sizeof(ce::dynamic_array<int>) == 16`,
   정렬·무브온리·예외 테스트 통과.
5. **(C0 go인 경우만)** C0 기준선 대비 할당 총량이 비퇴행이고 **개선이 수치로
   있다** (없으면 §5 C4대로 되돌린다).
6. 회귀 세트 · 프리팹 왕복 검사 착수 전과 동일 판정.

## 7. 하지 않을 것

- **`std::vector` 전면 치환** — 1,030곳을 건드릴 이유가 없다. 측정된 축에서만.
- **realloc / `mi_expand`** — §2.3.
- **SBO 재도입** — §2.3의 4조건 전이면 안 된다.
- **`ce::` 컨테이너 alias 20여 개 부활** — 죽은 이유가 소비자 부재다.
  `ce::dynamic_array`가 자리를 잡은 뒤에도 나머지는 필요할 때만 하나씩.
- **`std::string` 대체** — 별개 축(`Symbol` 인터닝)이 먼저다.
- **`reserve` 상수 계측** — 초안 C0. 총합 70KB라 잴 가치가 없다(§5 C0).
  아래 잡무로 격하한다.

### 7.1 잡무 (트랙과 무관, 언제든)

- `RenderPassData.cpp:69-70`의 `reserve(500)` 둘은 **죽은 코드다**
  (`PushRenderQueue`/`PushUIRenderQueue` 호출자 0). 큐 자체와 함께 철거 대상.
- `ReflectionRegister.h:26-27`의 128은 등록 타입 수이므로 **재는 게 아니라
  세어서** 맞추거나 지운다.
- `TagManager.cpp:8-13`의 32·300은 `TagManager.asset`의 태그·레이어 수에서
  나온다. 마찬가지로 센다.
- `Scene.cpp:60-61`의 3000은 저작 최대 씬(68 오브젝트)의 44배다. 다만 런타임
  프리팹 인스턴스화로 늘어나므로(최대 프리팹 혼자 178 오브젝트) 근거 없는
  값이라는 것과 틀린 값이라는 것은 다르다. **지울지 줄일지는 60KB짜리
  판단이므로 서두르지 않는다.**

## 8. 리스크

- **또 하나의 죽은 컨테이너가 될 위험** — 이 저장소의 대표 실패 양식이다.
  §4가 소비자를 먼저 못 박고, §5 C0가 관문이 되고, C4가 되돌림 조건을
  명시하는 이유.
- **리플렉션 특수화 누락 시 조용한 데이터 유실** — C1이 이를 컴파일 오류로
  바꿔 놓았으므로 C2에서 특수화를 빠뜨리면 빌드가 막힌다. 위험이 해소된 것이
  아니라 **발현 시점이 앞당겨진 것**이다.
- **예외 안전성** — `InlineVector` 리뷰가 남긴 미검증 경로와 같은 함정.
  C2 테스트에 "던지는 T"를 반드시 포함한다.
- **디버그 빌드 이득은 이 컨테이너로 오지 않는다** — 가장 큰 축(§1.1)은
  빌드 구성(`_ITERATOR_DEBUG_LEVEL`) 문제다(§2.4). 이 문서의 이득을 그것으로
  부풀리지 않는다. C0-2가 그 크기를 따로 잰다.
- **★ 이 문서가 스스로 커지는 것** — 초안의 C0와 기능 9번이 그랬다. "만들면
  좋을 것"을 적으면 목록이 늘고, 늘어난 목록이 다시 트랙의 정당성처럼 읽힌다.
  §2.4의 층 구분과 §5 C0의 관문이 그것을 막는 장치다.

## 9. 관련 문서

- [UtilityFrameworkModernizationPlan.md](../plans/UtilityFrameworkModernizationPlan.md) — 상위 계획
- [SceneGraphRedesignPlan.md](../plans/SceneGraphRedesignPlan.md) — K2 스테이지 B 폐기 결정, SBO 재적용 기준
- [ReflectionRedesignPlan.md](../plans/ReflectionRedesignPlan.md) — 리플렉션 표면
- [PPLContainerMigrationAnalysis.md](../analysis/PPLContainerMigrationAnalysis.md) — PPL 컨테이너 축
