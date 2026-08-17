# 컴파일타임 리플렉션 전환 — 헤더툴 은퇴와 typed 방문 계약 (PHASE 18)

2026-08-16. 근거: [ReflectionSystemAnalysis.md](ReflectionSystemAnalysis.md)(전수 실측 분석 — 병폐·비용·죽은
코드 목록은 그 문서가 정본, 여기 반복하지 않는다). 방향 결정: "qlibs/reflect
**류의 설계**로 전환" — 라이브러리 채택이 아니라 자작 컴파일타임 리플렉션이며,
기법은 집합체 제약이 없는 **멤버 포인터 튜플 명시 메타**(glaze식)를 정본으로,
진짜 집합체는 구조적 바인딩 자동 반영을 보조로 쓴다(분석 §8.3·§8.5 경로 (다)).

목표 사슬:

```
[[Property]] 어노테이션 + 정규식 헤더툴 + generated.h 154개 + any/function 이중 소거
    → static constexpr auto reflect = Meta::members(&T::m_a, ...)  (표기 한 곳)
    → 컴파일타임 typed 방문 (직렬화·인스펙터·쿠킹이 실제 타입 T&를 본다)
    → 런타임 브리지 = 타입당 함수 포인터 소수 (씬 로드 디스패치·C# 왕복용)
    → 헤더툴 3종·generated.h·pre-build 스캔·원본 in-place 재작성 소멸
```

산출이 곧 다른 트랙의 족쇄 해제다: 병폐 A 소멸 → K2 SBO 재개, 병폐 B 소멸 →
E5 unique_ptr 소유 완주, typed 순회 → SerializationPlan D2(쿠킹) 성능 상한 제거.

---

## 1. 결정 사항 (분석에서 확정된 것)

| 결정 | 근거 |
|---|---|
| 명시 메타(멤버 포인터 튜플)가 정본 — 컴포넌트 154타입 **수술 없이** 적용 | 분석 §8.3, 멤버 포인터는 가상·상속·private 무관 |
| 집합체(Mathf·설정 구조체)는 자동 반영 보조 경로 | 표기 0, PFR/reflect 기법 |
| 멤버명은 NTTP `source_location` 추출(매크로 문자열화 폴백 준비) | YAML 필드명 = 멤버명 유지 → 자산 호환 |
| typeID 런타임 정본 = 컴파일타임 FNV-1a 64(기존 죽은 `MakeTypeID` 부활) · 디스크 정본 = K1-b UUID 유지 | 분석 F-1·RF2, 층 분리 원칙 |
| ScriptReflect 갈래·핫리로드 스코프 기계는 **삭제**(통합 아님) | ModuleBehavior 사망, `ScriptRegister`/`BeginScope`/`UnRegisterScope` 호출부 0건 |
| 마이그레이션은 **어댑터 공존** 방식 — 새 메타에서 기존 `Meta::Type` 테이블을 생성해 소비자 무변경으로 타입을 먼저 이전, 소비자 재작성은 그 뒤 | 매 슬라이스 그린 유지 (§3 CT4) |
| P2996(C++26)이 MSVC에 오면 명시 표기만 접는다 — 계약(typed 방문)은 그대로 | 분석 §8.5 |

기각: (가) 생성기만 교체 — 병폐 잔존. (나) 매크로로 현 계약 조립 — 병폐 잔존.
(라) 집합체 분리 선행 — 수술 불필요해졌으므로 트랙 L·E 완성 후의 자연 승격으로
강등.

## 2. 실측 요약 (변경 대상의 크기)

- 등록 타입: 클래스 76(`RegisterReflect.def`) + 열거형 31. generated.h 154개·2,547줄.
- 소비자: 직렬화(`ReflectionYml.h`+`ReflectionYamlTemplete.h`) · 인스펙터
  (`ReflectionImGuiHelper.h` 994줄) · `ComponentFactory.cpp`(17분기+Deserialize 30호출) ·
  `PrefabUtility`(오버라이드 시딩) · `ConsoleCommandSystem`(11건) · Undo
  (`MetaStateCommand.h`) · `GameObjectCommand.h`.
- 죽은 표면(삭제 확정): `InvokeMethodByMetaName`·`Method` 체계(에디터 버튼 외 소비 0) ·
  `ClassAutoRegistrar` · `ScriptReflect` 매크로 6건 · `ScriptRegister`/`UnRegister` ·
  TypeCaster 스코프 기계(`BeginScope`/`EndScope`/`UnRegisterScope`+추적 맵 5개) ·
  ScriptReflectionHeaderTool 2종 바이너리 · 인스펙터 `vector<int>` 중복 분기.
- 헤더 전파: `Core.Minimal.h:9` → 280 TU에 yaml-cpp·magic_enum, TU당 static 싱글턴 6종.

## 3. 트랙 CT — 슬라이스

### ✅ CT0 — 기준선·안전망 (2026-08-16, 잔여 2건)

구현: `reflect.golden`(등록 전 타입 default-Serialize 덤프)·`perf.reflect`
(직렬화·소환 반복 계측) 콘솔 명령 + `Registry::GetAllTypeNames` +
`Tools/regression/verify-reflection-golden.ps1`(`-Baseline` 모드, run-all 배선 —
골든 없으면 건너뜀). 골든은 씬 콘텐츠에 기대지 않는 타입 커버리지 전수
덤프로 설계 — 씬 기반이면 GUID가 섞여 diff가 성립하지 않는다(실측:
`m_instanceID`·BT 노드 `ID`/`ParentID` 3키가 실행마다 달랐고, 검증 스크립트가
그 세 키만 자리 대조로 정규화한다. 비정규화 상태의 실패가 곧 음성 대조 증명).

**기준선 (2026-08-16 · Debug x64 · 오브젝트 3개 씬 · 반복 50):**

| 계측 | 값 |
|---|---|
| 골든 커버리지 | **76/76 타입 직렬화 · 팩토리없음 0 · 실패 0 · diff 0** |
| 씬 전체 `Meta::Serialize` | **10.7~13.3 ms/회** (4회 실측 범위) — 오브젝트 3개에 이 값이다 |
| `InstantiatePrefab`(BTProbe) | **3.0~3.8 ms/회** — MobSpawner가 스폰마다 무는 값 |
| 리플렉션 헤더 터치 재빌드 | **4분 29초** (내용 무변경·타임스탬프만, 유니티 빌드 TU 23개 재컴파일) |
| 무변경 증분 빌드 | **1분 29초** → 헤더 1개 터치 순비용 ≈ **3분** (§3.4 전파의 실측, CT3 목표치) |

부수 실측: `ConsoleCommandSystem::Execute`의 else-if 사슬은 이미 MSVC 블록
중첩 한계(C1061) 직전이다 — 명령 2개를 사슬에 보탰다가 컴파일이 깨져 조기
분기(사슬 앞 early-return)로 옮겼다. 이후 명령 추가는 같은 수법을 쓸 것.

잔여(후속 계측, 판정 아님): ① 씬 로드 전체 벽시계(`LoadSceneImmediate` 구간
스팬 — PHASE 14 P1 캡처가 서면 겸용), ② 인스펙터 열림 상태 에디터 프레임
비용(에디터 상호작용 필요 — CT1의 개선 대상이므로 CT1 착수 시 수동 캡처로 선측정).

### ✅ CT1 — 즉효 저위험 (2026-08-16)

- 인스펙터 문자열 `Find` 14건 → typeID (최대 수확: `InspectorWindow.cpp:171`의
  `Find(component->ToString())` — 매 프레임 컴포넌트마다 문자열 생성+해시 조회,
  GENERATED_BODY의 m_name=타입명 관행에 기댄 우회였다). `MenuBarWindow.cpp:2479`
  (런타임 임의 문자열)만 범위 밖 — CT6에서 구조와 함께.
- `FindYamlSerializer`/`FindYamlVectorEntry` 선형 스캔 → **함수-로컬 매직
  스태틱** 해시맵(네임스페이스 스코프 맵은 TU마다 동적 초기화 → SIOF 함정,
  정찰 §2 실측 반영).
- 조회 순서 역전(스칼라 먼저) — Serialize·Deserialize 양쪽. 안전 근거: 스칼라
  테이블 23종 ↔ Registry 76타입 **교집합 공집합**(정찰 실측). 부수 발견:
  두 함수의 기존 조회 순서가 서로 달랐다(enum→struct vs struct→enum — 문서화
  안 된 비대칭, 이번에 정렬). 단일 호출처 헬퍼 2개는 본문 흡수.
- 벡터 편집 5블록: 접힘 검사를 복사보다 앞으로 + `vector<int>` 중복 분기
  (도달 불가) 삭제.
- `prop.name` 포인터 비교 → `strcmp`(F-5). `FieldEnd` type_name 매직 스태틱화.
- 부수 수정 2건: DrawMethods 공유 static 맵의 **동명 메서드 키 충돌**(타입
  경계 넘어 입력값 누출 — 키에 타입명 포함) · `std::string`을 varargs(%s)에
  넘기던 UB.
- 주의 유지: `ReflectionImGuiHelper.h:737` 인근의 이미-죽은 재조회 분기는
  건드리지 않았다(정찰 경고 — 실수로 "고쳐 살리면" 새 UI 경로가 열린다).

계측: `InstantiatePrefab` **3.0~3.8 → 2.0ms/회 (약 35%↓)** · 씬 Serialize
10.4ms(범위 내). 검증: 빌드 그린 · 골든 diff 0. 인스펙터 프레임 계측은 CT0
잔여 ②와 함께 후속.

### ✅ CT2 — 죽은 갈래 일소 (2026-08-16, `f13eaab7`)

정찰 실측(네 방향 증거: 호출처 0 · 베이스 virtual 소멸 · 빌드 배선 부재 ·
주석 자백)으로 확정 후 삭제: 스크립트 매크로 6종 + **BT/Ani BODY 4종**(정찰
추가 발견 — Dynamic_CPP 전속), TypeCaster 스코프 기계(맵 5종 포함),
`Registry::ScriptRegister`/`UnRegister`, `InvokeMethodByMetaName`,
`ClassAutoRegistrar`, 고아 바이너리 2종. **유지 확정**: `GENERATED_BODY`
(ScriptBinder 19곳 라이브), `Method` 체계(InspectorWindow→DrawObject→DrawMethods
라이브 경로), `MetaGenerator.exe` 배선, 구포맷 가드. Dynamic_CPP.vcxproj는
`54ea26bc`(9-4)에서 이미 삭제됐음을 sln·git 이력으로 확인 — pre-build 배선
제거는 이미 완료 상태였다. RetentionDecision R3·부수 근거 정정 완료.
검증: 빌드 그린 · 골든 diff 0. 순삭감 -255줄.

### ✅ CT3 — 헤더 전파 절단 (2026-08-16)

- `Core.Minimal.h`에서 `Reflection.hpp` 분리. 소비 TU 직접 include는 빌드
  에러 반복이 아니라 **전수 선반영**(51파일) — 유니티 빌드가 앞선 TU의
  include를 공급해 누락을 숨기므로 에러 주도 수복은 불완전하다(정찰 §2:
  후보 전수 산출 → 프로바이더 4종(Component.h·GameObject.h·SceneManager.h·
  ComponentFactory.h 직접 보유 확인) 경유 파일 제외 → 거짓양성 4건 수동 판별).
- 싱글턴 포인터 7종 전부 `inline auto`로(ODR 위험 없음 — 정찰 §8: extern/
  전방선언 참조 0건, DLL 경계 동일성은 DLLCore::Singleton이 이미 보장).
- `ReflectionRegister.h`: yaml-cpp·ClassProperty(본문 사용 0회) 절단,
  UndoManager+MetaStateCommand → `ReflectionUndo.h` 분리(소비자는
  Reflection.hpp 사슬이 물어 줘 include 변경 불요). MetaStateCommand.h의
  숨은 순서 의존(include 0개로 앞줄 include에 기생)도 명시 include로 해소.
- MetaGenerator 콘텐츠 가드 3곳(generated.h는 mtime→콘텐츠 대체, 원본 헤더·
  def는 신설 — **원본 헤더 무가드 재작성이 최악이었다**: 삽입이 없어도 매
  실행 전량 재작성해 mtime을 갱신). 새 exe 배치, 실행 후 변경 파일 0 검증.

**재측정 (기준선 대비):**

| 계측 | CT0 기준선 | CT3 후 | 비고 |
|---|---|---|---|
| 무변경 증분 빌드 | 1분 29초 | **15.6초 (5.7배)** | 진범은 생성기 무가드 재작성 — 매 빌드가 "무변경"이 아니었다 |
| 리플렉션 헤더 터치 재빌드 | 4분 29초 | 4분 08초 | 소폭 — Component.h가 Reflection.hpp를 직접 물어 컴포넌트 전체 재컴파일은 정당한 전파. Core.Minimal 절단의 실익은 리플렉션 무관 TU의 yaml-cpp 파싱 소멸 |

검증: 빌드 그린(첫 시도) · 골든 diff 0.

### ✅ CT4 — 신 코어 + 어댑터 + typeID 교체 (2026-08-17, 2커밋 분리)

**CT4-a (`6f7ec6ca`)** — `ReflectionMeta.h`: `CtReflect()`(constexpr 멤버 포인터
튜플) + `ForEachMember`(typed 방문) + `AdaptReflect`(기존 Type 테이블 다리).
설계 확정 3건(계획 대비 정밀화):
- CtReflect는 static 데이터가 아니라 **함수** — 매크로가 클래스 상단(멤버 선언
  앞)에 놓이는 관례를 지키려면 complete-class 문맥이 필요하다.
- 이름은 NTTP가 아니라 **매크로 문자열화 정본** — MSVC 버전 민감 회피,
  현행 YAML 키와 동일 보장. (NTTP 추출은 P2996 착지 시 재평가)
- 파일럿 4타입(MeshRenderer·BoxCollider·LightMapping·ShadowMapPassSetting)
  이전, generated.h 4개 삭제, `RegisterReflectManual.h` 신설(def 스캔 밖 등록,
  CT5에서 정본 승격). 골든 diff 0 = 어댑터 바이트 동등 증명. 함정 추가 발견:
  **주석에 이중 대괄호 어노테이션 원문 금지**(생성기가 regex_search로 훑는다).

**CT4-c (2026-08-17, 사용자 결정)** — 표면을 **매크로 프리 · P2996 유사**로
교체. `^^T`/`[:r:]`는 표현 불가하므로 라이브러리 근사로:
`meta::reflect<T>()` · `meta::get<m>(obj)`(스플라이스) · `meta::members_of<T>()` ·
`meta::identifier_of(m)` · `meta::for_each_member(obj, f)`(template for) ·
선언은 `static consteval auto reflect() { return meta::describe<T, Parent,
&T::m_a, ...>{}; }` 두 함수뿐(매크로 0). 성립 근거 실측: **VS 18 __FUNCSIG__가
멤버 포인터 NTTP를 `&Owner::m_value`로 깨끗하게 표기**(중첩 네임스페이스·
private 포함, 프로브 실행으로 확인) — CT4-a에서 "MSVC 버전 민감"으로 보류했던
NTTP 이름 추출이 이 툴체인에선 성립한다. 카나리아 static_assert가 표기 변화를
컴파일 타임에 잡는다(툴체인 업그레이드 시 최우선 확인 지점). 부수 발견:
`type_name`은 **한정 이름**을 준다(네임스페이스 포함) — 등록 76타입은 전부
전역이라 골든 무영향, 단 CT5에서 네임스페이스 타입을 만나면 Type::name이
한정 이름이 됨을 유의. ct_property/ReflectionMetaField* 매크로는 삭제.
검증: 골든 diff 0(추출 이름 = 구 문자열화 이름 바이트 동등) · 회귀 전체 통과.
P2996 착지 시 reflect()/describe 선언부만 컴파일러 제공으로 접히고 소비
표면은 유지된다.

**CT4-d (2026-08-17, 사용자 결정 2건)** — 표기 정련:
- **빌더 표현식 + 멤버 속성**: `meta::describe<T>(meta::base<Parent>(),
  meta::member<&T::m>(meta::range(0.0f, 1.0f), meta::displayName("...")), ...)`.
  속성은 어댑터가 무시(골든 무영향)하고 CT6 인스펙터가 소비한다(range →
  슬라이더 한계). BoxCollider 마찰·반발 계수가 살아있는 예시. 기술 제약:
  속성 튜플 탓에 member_info는 비구조적 — NTTP 스플라이스는 `get<&T::m>(obj)`,
  서술자 값은 `get(m, obj)`/`m.get(obj)`.
- **Reflect() 보일러플레이트 외부화**: reflect/Reflect 대소문자 쌍 금지 지적 →
  선언부는 `describe()` **하나**로, 런타임 Type은 외부 창구 `Meta::TypeOf<T>()`
  (레거시 `T::Reflect()` 폴백 내장, CT7에서 폴백 가지 소멸)가 공급. CRTP는
  상속 목록 오염(타입당 표기 잔존 + 기반 클래스 추가)이라 기각.
- 함정 실측 1건: 파일럿에서 Reflect()를 없애자 `HasReflect` 컴파일타임 분기
  (벡터 매퍼)가 `as<T>()` 폴백으로 떨어져 `YAML::convert<T>` 미정의 에러 —
  **`HasRuntimeType`(= HasReflect ∥ HasDescribe) 통합 콘셉트**를 ReflectionType.h
  에 신설해 분기 2곳 교체. CT5 본대 이전이 타입마다 밟았을 함정의 선제 봉합.

**CT4-b** — typeID 정본 교체. `type_name<T>()`은 참조만 있고 정의가 없어
MakeTypeID가 실은 **인스턴스화 불가능한 죽은 코드**였음이 드러남 — __FUNCSIG__
기반으로 구현(선행 class/struct/enum 키워드 제거 — 표기 변경이 정체성을 바꾸지
않게 + `ComponentTypeID = fnv1a_64("Component")`가 층을 넘지 않고 값을 재현하는
전제). `GetTypeID` → `static constexpr MakeTypeID<T>()`. 구 해시 리터럴 1곳
(ReflectionYml.h ComponentTypeID) 갱신. `ExtractTypeFromYAML` 이름 완화(경고+
수용, 재저장 치유). 관리 측 typeID 의존 0건 grep 확인.

**검증**: 교체 diff 전수 검수 — **변경 31줄 전부 `타입명: ID` 헤더, 비-ID 변경
0**(프로퍼티 값·이름·구조 바이트 동등) 확인 후 골든 재기준선(의도된 포맷 변경).
회귀 전체 통과 — 구 typeID를 품은 기존 프리팹이 이름 완화 경로로 실로드됨.

### 원계획 (CT4 착수 전 설계 — 이행 기록 위해 보존)

- `Meta::members(&T::m_a, ...)` + 컴파일타임 `for_each`(typed 방문) + NTTP
  멤버명 추출(**static_assert로 추출 결과를 리터럴과 대조** — MSVC 버전 민감,
  실패 시 매크로 문자열화 폴백) + 부모 메타 concat + FNV-1a 64 typeID
  (`TypeTrait.h`의 죽은 `MakeTypeID` 부활·`GUIDCreator` 대체) + 집합체 자동 경로.
- **어댑터**: constexpr 메타에서 기존 `Meta::Property`/`Type` 런타임 테이블을
  생성하는 브리지 — 소비자(직렬화·인스펙터·팩토리)는 무변경으로 돈다. 이
  어댑터 덕에 CT5의 타입 이전이 타입 단위로 원자적이고 항상 그린이다.
- **typeID 교체 동반 조치**: 씬 YAML의 `타입명: typeID` 헤더 검증
  (`ExtractTypeFromYAML` — 이름+ID 일치 요구)이 새 ID와 어긋난다. K1-b UUID
  우선은 이미 있으므로, **이름 일치 시 ID 불일치를 경고+수용**으로 완화하고
  재저장 시 새 ID를 쓴다(SerializationPlan "호환 무시" 창과 같은 창구.
  UUID 없는 구파일의 유일한 폴백이 이름 검증이므로 완화는 로그 필수).
- 파일럿: 집합체 2종(Mathf 계열·설정 구조체) + 컴포넌트 2종(단순 1 —
  예: BoxColliderComponent, 복합 1 — MeshRenderer: shared_ptr·vector·enum 혼합).
- 검증: 파일럿 타입 골든 diff 0, 회귀 그린.

### ✅ CT5 — 본대 이전 (2026-08-17, 단일 스윕)

- **`meta::method` 확장 선행**: 멤버 함수 NTTP는 __FUNCSIG__ 표기가 다르다
  (`RET __cdecl Owner::Name(Args)` — 프로브 실측). 별도 파서 + 카나리아.
  어댑터가 MethodOnly(멤버 0) 타입의 빈 프로퍼티 뷰를 허용하도록 수정.
- **결정적 스크립트 스윕** (에이전트 아님 — generated.h는 기계 생성물이라
  구조가 100% 규칙적, 바이트 단위 줄 편집으로 CP949 원문 보존·삽입은 ASCII만):
  74헤더 이관(프로퍼티 411·메서드 7), generated.h 79개 삭제(고아 6건 포함 —
  호출 0·등록 0인 부패한 기계 산출물), 어노테이션 일소.
- 등록 정본 승격: `RegisterReflectManual.h`가 76종 전체(구 def 승계 + 파일럿
  4종 합류). 재생성된 def는 **빈 목록**(어노테이션 0) — CT7에서 은퇴.
- 직접 `GameObject::Reflect()` 호출 3곳 → `Meta::TypeOf<GameObject>()`.
- 수복 2건: 타 타입 generated.h를 include하던 교차 참조 1건
  (SpriteSheetComponent→ImageComponent), Socket.h의 죽은 어노테이션 4건
  (Serializable 없이 Property만 — 애초에 스캔된 적 없는 장식).
- 검증: 빌드 그린 · **골든 diff 0 (76/76 전 타입이 describe 경로에서 바이트
  동등)** · 회귀 세트 전체 통과.

### ◐ CT6-a — typed 직렬화기 + 런타임 브리지 (2026-08-17)

`ReflectionTypedYml.h`: `meta::for_each_member` 위의 typed Serialize/Deserialize
— 프로퍼티당 `std::function` 간접호출 2회 + `std::any` 값 복사 + 조회 폴백이
if constexpr 트리로 접혔다. 런타임 디스패치는 `Typed::TypeOps`(타입당 함수
포인터 2개, RegisterReflectManual이 등록) — `Meta::Serialize/Deserialize`
진입점에서 우선 디스패치하므로 **호출처 전체 무변경**. 레거시 Property 워크는
미등록 폴백으로 남고 CT7에서 소멸.

파리티 재현 목록: 스칼라 23종 인코딩(Flow 맵 좌표계·r/g/b/a·ToString류) ·
Flow 스타일은 스칼라 원소 벡터에만 · 컴포넌트 실타입 헤더+m_typeUUID 주입
위치(키 순서: 헤더→UUID→부모 프로퍼티→자신) · `[not support type]` 마커 ·
부재 키 스킵 · 포인터 원소 벡터(컴포넌트 목록)는 복원 안 함(팩토리 몫) ·
원시 포인터 재역직렬화 누수까지(F-2 — E5에서 해소 예정).

**계측 (CT0 기준선 대비):** 씬 Serialize **10.7~13.3 → 6.9ms/회 (~40%↓)** ·
InstantiatePrefab **3.0~3.8 → 1.35ms/회 (~60%↓)**. 골든 diff 0 첫 실행 통과.

함정 실측 3건: ① `if constexpr`의 &&는 인스턴스화를 단락하지 않는다 — 불완전
포인티(NodeEditor*)의 is_default_constructible hard error → 중첩 if constexpr.
② 오버로드 가시성 기반 스칼라 콘셉트는 비스코프드 enum이 `HashedGuid(size_t)`
비명시 생성자로 암묵 변환돼 오판(LightType) → 정확 타입 목록으로. ③ 레거시
`FromYamlScalar<HashedGuid>`의 `as<uint32_t>`는 FNV64 이후 잠재 로드 버그
(BadConversion) — 로드 측만 `as<size_t>`로 수정(쓰기 불변, 골든 안전).

### ◐ CT6-b — 인스펙터 속성 소비 (2026-08-17, 스코프 조정)

`meta::range`/`meta::displayName`이 엔드투엔드로 실동한다: `Property`에 투영
필드 4종(hasRange/rangeMin/rangeMax/displayName — 꼬리 추가라 기존 위치 지정
집합체 초기화 무영향) → 어댑터 `BuildPropertyFrom`이 member_info 속성을 주입
→ 인스펙터 float/int 위젯이 range면 Slider, displayName이면 라벨 교체(ID는
prop.name 유지 — 위젯 상태 안정성). BoxCollider 마찰·반발 계수가 첫 소비자.

**스코프 조정(정직 기록)**: "타입당 typed Draw 테이블 전면 전환"은 이연 —
인스펙터에는 골든 같은 자동 파리티 검증이 없고, 레거시 체인에는 드래그드롭
페이로드·에디터 시스템 결합(SCENE_OBJECT/Textures payload·EditorImGuiTexture)
같은 수동 검수 필수 경로가 많다. 속성 소비는 검증 가능한 최소 배선으로 먼저
싣고, 전면 전환은 에디터 수동 검수가 가능한 세션에서 별도 슬라이스로.

### ✅ CT6-c — 인스펙터 typed Draw + A/B 토글 (2026-08-17, `df551178`)

`ReflectionTypedDraw.h` — meta 서술 기반 컴파일타임 위젯 디스패치(스칼라·
enum(magic_enum 직결)·벡터 5종(원본 직접 편집 — 임시 복사 소멸)·포인터/
구조체 재귀), range/displayName 소비, 멤버 포인터 캡처 언두. 등록 목록은
X-매크로(REFLECT_TYPE_LIST)로 공유 — 런타임 등록·직렬화 썽크·Draw 썽크가
한 목록을 소비하고, 위젯 트리 인스턴스화는 InspectorWindow TU에 격리.

**검수 도구를 만들어 검수까지 완료**: `meta::g_inspectorTypedDraw` 토글 +
`inspector.typeddraw` 콘솔 명령으로 A/B, 회귀 하네스의 창 캡처로 에디터를
스크립트 구동(scene.new → component.add BoxCollider/SpriteRenderer →
scene.select → 캡처)해 typed/레거시 **픽셀 동등** 확인(BoxCollider range
슬라이더·SpriteRenderer enum 콤보 포함). 파리티 기준 실측: 레거시 포인터
특수 분기(GameObject·Texture 드래그드롭)는 prop.typeID를 포인티 타입과
비교하는 오류로 **사문**이었다 — typed는 라이브 경로(제네릭 재귀)와 맞추고,
드래그드롭 복원 여부는 별도 결정으로 남긴다. 함정: 콘솔 `wait`는 프레임
단위 — 빈 씬 200+fps에서 캡처 타이밍이 증발한다(무한 대기+외부 킬로 해결).

### ✅ CT6-d — 팩토리 분기 소멸 · OnDeserialized 후크 (2026-08-17)

`ComponentFactory::LoadComponent` **686줄 → 137줄**. 17개 타입 분기가 공통
경로(typed Deserialize → SetOwner → `TypeOps.postLoad` 훅) 하나로 접혔고,
타입별 후처리는 각 컴포넌트 소유의 `OnDeserialized([node])`로 이관됐다
(12종 — 애셋 GUID 해석·텍스처/폰트/시트 로드·Animator 컨트롤러 그래프·
Foliage 모델 탐색·Canvas UIManager 등록·Light/Camera/Foliage/Sprite/Mesh의
강제 활성 특이 동작 보존). 훅 유무는 `requires` 판별(양쪽 시그니처 지원),
없으면 nullptr — 순수 보일러플레이트 분기 5종(RigidBody·BoxCollider·
CharacterController·BehaviorTree·PlayerInput)은 흔적 없이 소멸.

**버그 수정(발굴)**: Image/Text/SpriteSheet의 `navigations`·Sound의
`localRolloffCurve` 수동 복원 루프는 **이식하지 않고 삭제** — 반영 멤버라
typed 역직렬화(CT6-a)가 채우는데 루프가 다시 append하면 이중 적재다. 이
루프들이 존재했던 이유도 실측으로 밝혀짐: 레거시 벡터 경로는
`MakeAnyFromRaw`에 vector 타입 캐스터가 없어 **setter가 조용히 스킵**되고
고아 벡터만 채우던 침묵 실패였다 — 수동 루프가 그 구멍을 메우고 있었고,
CT6-a가 구멍을 고치자 루프가 버그로 반전됐다.

주의: MeshRenderer 훅은 SetOwner 이후에 돈다(구 분기는 애셋 로드 후
SetOwner — 애셋 코드가 소유자를 안 씀을 감사하고 순서 통일). Canvas 훅은
소유자 shared_from_this가 필요해 이 순서가 전제다.

잔여(CT6 잔재·CT7로): 콘솔 `ApplyReflectedProperty`·프리팹 시딩은 Property
테이블 위에서 동작(이름 기반 — 값 접근 아님)이라 실익이 적어 어댑터와
운명을 같이한다.

### 원계획 CT6 (착수 전 설계 — 이행 기록 위해 보존)

- 직렬화: `ReflectionYml`을 typed 방문으로 — 3단 폴백·any 박싱·부모 노드
  재복사 소멸. 컨테이너는 제네릭 레인지(→ K2 SBO 재개 가능), 소유는 종별
  분해(unique_ptr 생성-이전 경로 — E5와 같은 슬라이스 권장).
- 인스펙터: 타입당 `Draw` 인스턴스화(전용 TU) — 프로퍼티당 선형 체인 소멸.
- `ComponentFactory` 17분기: 애셋 참조를 `AssetRef<T>` 류 래퍼 타입으로 —
  typed 방문자가 래퍼를 보고 로드 후처리를 공통 수행(분석 RF5 흡수).
- Undo(`PropertyChangeCommand`)·콘솔·Prefab 시딩 typed 전환.
- 검증: 골든 diff 0 + 회귀 + CT0 계측 재측정(씬 로드·스폰·인스펙터).

### ✅ CT7 — 은퇴·정산 (2026-08-17) — PHASE 18 완결

**은퇴 목록**: MetaGenerator 프리빌드 배선 6곳(vcxproj 2×3구성) · 헤더툴
바이너리(MetaGenerator.exe·pugixml.dll) · AutoRegisterCreateReflection 프로젝트
전체 · 빈 RegisterReflect.def + SceneManager 배선 · 레거시 매크로 사슬
(ReflectionField*/PropertyField/MethodField/meta_property/meta_method/
FieldEnd/PropertyOnly* 등 — ReflectionMecro.h는 생존자 3종만: GENERATED_BODY·
AUTO_REGISTER_CLASS·AUTO_REGISTER_ENUM) · 레거시 직렬화 Property 워크
(Serialize/Deserialize 본문 — 디스패치+미등록 오류 폴백만 잔존) ·
ComponentTypeID 상수 · Vector{Factory,Invoker,Mapper} 3종 헤더+부트스트랩
배선(레거시 워크 전용이었다) · 레거시 인스펙터 체인(DrawProperties 769줄) ·
A/B 토글·inspector.typeddraw 콘솔 명령. DrawProperties 직접 호출 11곳은
`TypedDraw::DrawOwnMembers`(부모·메서드 제외 등가)로 전환.

**존치 명시**: `meta::adapt` 어댑터와 Property/Method 테이블 — 콘솔
`ApplyReflectedProperty`(이름 기반 필드 설정)·프리팹 시딩(이름 기반 diff)·
`DrawMethods`(메서드 UI)·`DrawEnumProperty`가 소비한다. 이들은 값 접근이
아니라 이름/메타 질의라 typed 전환의 실익이 없다 — 어댑터는 "과도기 다리"
에서 "이름 기반 소비자의 정본 테이블"로 재정의된다.

**최종 계측 (CT0 기준선 → CT7):**

| 계측 | CT0 | CT7 | 배율 |
|---|---|---|---|
| 무변경 증분 빌드 | 1분 29초 | **7.1초** | **12.6×** (CT3 생성기 가드 15.6s → 프리빌드 소멸 7.1s) |
| 씬 전체 Serialize | 10.7~13.3 ms | 8.2 ms* | ~1.5× (*CT6-a 직후 6.9ms — 런별 편차 존재) |
| InstantiatePrefab | 3.0~3.8 ms | **1.37 ms** | **~2.5×** |
| generated.h | 154개 | **0** | — |
| 헤더툴 바이너리 | 3종 | **0** | — |
| 어노테이션 표기 | 2벌(어노테이션+생성 헤더) | **1벌(describe)** | — |

검증: 빌드 그린 · 골든 diff 0 · 회귀 세트 전체 통과.

### ✅ CT7-b — CRTP 정체성 베이스 (2026-08-17, 사용자 결정)

"GENERATED_BODY 없이 m_name·m_typeID를 개발자가 신경 쓰지 않는 법" —
`meta::identity<T, Base>`: 상속 선언 자체가 스탬핑을 수행한다.

- 판정의 핵심은 **m_name의 이중 의미**였다: typed 직렬화가 멤버를 직접
  읽고(지연 채움 불가) GameObject류는 인스턴스 이름이라(타입명 파생 불가)
  eager 생성자 스탬핑이 구조적으로 필요 → 생성자 타이밍을 소유한 CRTP가
  유일한 완전 자동화. 지연 typeid 브리지(반쪽)·봉인 생성 경로(우회 위험) 기각.
- Component 계열 31종(describe 부모 그래프로 판별) 스크립트 스윕: 상속절
  치환(`public P` → `public meta::identity<X, P>`) · GENERATED_BODY 18건 →
  `X() = default;` · 수동 스탬핑 12파일 제거. 중간 베이스 체인(UIComponent →
  ImageComponent)은 생성 순서(베이스 우선·말단 최종 덮어쓰기)가 계약.
- **GameObject·Scene 등 비컴포넌트는 제외** — m_name이 인스턴스 이름인
  집합은 수동 스탬핑 유지(identity를 쓰면 이름이 타입명으로 덮인다).
- GENERATED_BODY 매크로 최종 삭제 — ReflectionMecro.h 생존자는
  AUTO_REGISTER_ENUM 1종(## 이름 결합 — 매크로 아니면 불가능한 유일한 자리).
- 검증: 빌드 그린(첫 시도) · 골든 diff 0(m_name 직렬화 값 파리티 증명) ·
  회귀 전체 통과.

### ✅ CT7-c — EnumRegistry 은퇴·매크로 0종 (2026-08-17, 사용자 점검 요청)

"EnumRegistry 점검 + AUTO_REGISTER_ENUM 제거 가능 여부" — 소비 실측이
등록소 자체의 사망 선고로 이어졌다.

- **소비 3곳 전수**: 콘솔 `ApplyReflectedProperty`(enum 이름→값 변환) ·
  `DrawEnumProperty`(콤보) · MeshRenderer 헬퍼(`FindEnum("MaterialRenderingMode")`
  문자열 리터럴). 셋 다 Property를 경유하거나 enum 타입을 정적으로 알고 있었다
  — 이름 키 런타임 조회가 구조적으로 불필요.
- **대체**: `Property::enumType`(꼬리 추가) — `MakeEnumPropertyImpl`이
  `create_enum_type<EnumT>()`의 함수-로컬 static 정본을 연결한다. adapt 경유
  모든 enum 프로퍼티가 자동으로 표를 얻으므로, 매크로를 빠뜨린 열거형이
  콘솔에서 조용히 "지원하지 않는 타입"으로 빠지던 구멍과 이름 표기 불일치
  (ToString vs magic_enum) 실패 여지가 함께 소멸. DrawEnumProperty의 이중
  조회(파라미터로 받고 이름으로 또 찾기)도 제거.
- **삭제**: EnumRegistry·MetaEnumRegistry·EnumAutoRegistrar·FindEnum ·
  AUTO_REGISTER_ENUM 줄 29개(19 헤더) · **ReflectionMecro.h 파일 자체**
  (매크로 0종 도달 — CT7-b의 "매크로 아니면 불가능" 판정은 '흩어진 정적
  등록자'라는 전제 위였고, 등록 행위 자체가 사라지면 전제도 사라진다).
  Reflection.hpp는 ReflectionFunction.h 직접 include로 재표적(소비 60여 파일
  터치 없이 관문 유지). Dynamic_CPP의 KoriEmoteSystem.h 1건은 빌드 밖
  (vcxproj 무참조)이라 데이터 보존 정책대로 미수정.
- 검증: 빌드 그린 · 골든 diff 0 · 회귀 세트 전체 통과.

### ✅ CT8 — 함수형 describe() → canonical 변수 T::reflection (2026-08-17, 사용자 지시)

타입당 서술자 객체 하나: `inline static constexpr auto reflection =
meta::describe<T>(...)` (클래스 하단 — 정적 데이터 멤버 초기치는
complete-class 문맥이 **아니라** 참조 멤버 선언 뒤여야 한다. CT4의 "함수라
상단 배치 가능" 규칙이 정확히 뒤집힌 것).

- **파일럿 선행 증명**: 불완전 Self의 PTM NTTP로 __FUNCSIG__ 이름 추출이
  정적 멤버 초기치에서도 성립함을 selftest 카나리아(공개+속성+파라미터
  메서드 / 부모 체인+private 멤버 / 메서드 전용)로 확인 후 본대 착수.
- **전환 규모**: 타입 77종(본대 76 + GameObject·Scene 등 비컴포넌트 포함
  전 서술 타입) 결정적 스크립트 + selftest Canary 수동 1종 = describe()
  멤버 함수 78개 소멸. 빌더 자유 함수 meta::describe<T>()는 정식 표기로 존치.
- **API**: reflect()/members_of()가 consteval 값 반환 → constexpr
  `const auto&` 반환(canonical 주소 계약을 static_assert로 고정:
  `&meta::reflect<T>() == &T::reflection`). HasDescribe → HasReflection
  (T::reflection 판별) 개명. 레거시 HasReflect·HasRuntimeType·TypeOf의
  Reflect() 폴백 가지는 정의 잔존 0건 검색 증명 후 삭제.
- **정적 desc 사본 4곳 제거**: for_each_member·adapt·DrawFrame·
  DrawOwnMembers의 `static constexpr auto desc = T::describe()` — 특히
  DrawFrame<T, Owner>는 Owner 조합마다 같은 T의 서술자를 중복 물질화했다.
  타입 질의(TypedYml 4곳)는 `decltype(T::reflection)`으로 ODR-use 없이 해소.
- **메모리 실측(SzProbe — 미정의 템플릿 에러 메시지로 sizeof 채집)**:
  무속성 member_info 1B · 속성 2종 24B · Canary 서술자 48B · MeshRenderer
  서술자 9B · 인스턴스 216B(불변). [[msvc::no_unique_address]]는 attributes/
  paramNames/type_desc 튜플 전부에 적용해도 **전 항목 크기 불변** — MSVC
  std::tuple이 요소를 _Tuple_val 멤버로 싸서 빈 요소 겹침 불성립 → 기각.
- 바이너리: Academy_4Q .rdata −3,968B·파일 −3,584B, Player .rdata −2,688B
  (TU별 desc 사본 소멸 방향과 일치). Release 전후 비교는 전환 전 Release
  산출물 부재로 불가(한계 명시).
- 검증: 빌드 그린(첫 시도) · 골든 diff 0(76타입) · 회귀 세트 전체 통과 ·
  git diff --check 통과 · 잔여물 검색 0건(describe 멤버·T::describe()·
  정적 desc 사본·값 복사).

### ✅ CT9 — meta::schema 로컬 스키마·identity 단일 원본·라이브러리 분리 (2026-08-17, 사용자 확정안)

CT8 변수형을 하루 만에 대체한 최종 표기 — 함수형 레시피의 부활이되, 물질화는
canonical 한 곳:

```cpp
class BoxColliderComponent
    : public meta::identity<BoxColliderComponent, Component>
{
public:
    static consteval auto reflect()
    {
        return meta::schema<Self>(
            meta::field<&Self::m_boxExtent>,
            meta::field<&Self::staticFriction>
                .with(meta::range(0.0f, 1.0f)),
            meta::field<&Self::density>);
    }
};
```

- **상속의 유일 원본 = 클래스 선언**: identity<T, Base>가 identity_descriptor
  (meta_identity)를 공개하고 schema<T>가 부모를 자동 추론 — `meta::base<>()`
  표기 소멸(33건). 자기 identity 없이 조상 것을 상속하면 static_assert 즉사
  (조용한 부모 소실 방지). 다중 상속 루트(GameObject·Prefab)는 수동
  meta_identity 별칭 2건, 무부모 44종은 국소 `using Self`.
- **로컬 스키마 계약**: reflect()는 자기 선언 필드·메서드만 적는다. 상속
  합성은 질의가 계산 — localFields/fields(평탄)·localMethods/methods·
  bases(type_list)·direct_base_t·declaringType(owner_type — 인스펙터 그루핑).
- **계층 분리**: MetaSchema.h 신설 — std 전용(엔진 include 0, 별도 라이브러리
  독립 가능 경계). 이름 추출·field/.with(가변)·method/.params·schema·of<T>
  외부 서술·reflect()·질의·for_each_field. 엔진 글루(ReflectionMeta.h)는
  identity 스탬핑·adapt·TypeOf·selftest. type_name은 라이브러리 복제본과
  TypeTrait 출력 동일성을 selftest가 교차 검증(갈라지면 typeID·YAML 헤더 붕괴).
- **이중 공급원**: in-class reflect() 또는 meta::of<T> 특수화(침투 0) — 동시
  존재는 static_assert. 소비는 meta::reflect<T>()(consteval 질의) 하나로,
  런타임 소비자(직렬화·인스펙터·어댑터)는 meta::schema_of<T> 변수 템플릿
  **한 곳**만 참조(CT8 canonical 주소 계약 승계 — 소비처별 정적 사본 0 유지).
- 함수형 복귀로 **상단 배치 부활**(본문은 complete-class 문맥 — CT4 근거 재적용).
- 스윕: 77타입 결정적 스크립트(identity 31·수동 별칭 2·plain 44) + 카나리아
  4종(루트 identity·수동 별칭+private·메서드 전용·외부 서술). 함정: cbuffer
  매크로 선언(MaterialFlow/MaterialInfomation)은 class|struct 패턴에 안 걸린다.
- 검증: 빌드 그린(첫 시도) · 골든 diff 0(76타입 — 부모 자동 추론의 바이트
  동등 증명) · 회귀 세트 전체 통과 · git diff --check 통과 · 구 표기 잔존 0.

### ✅ CT9-b — 열거형 자급화 + 구 코드 존치 전수 감사 (2026-08-17)

**magic_enum 은퇴**: 같은 원리(후보 값 범위의 __FUNCSIG__ NTTP 스캔, '(' 캐스트
표기 = 무효 판별)를 MetaSchema.h에 자급 구현 — meta::enum_entries<E>(값
오름차순·NUL 종단 이름·canonical 변수 템플릿)·enum_count·enum_name. 범위는
magic_enum 기본과 동일([-128,128], 부호 없는 기저 [0,128], 기저 한계 클램프)
이라 산출 집합·순서 파리티. 소비 2곳 전환: create_enum_type(ReflectionFunction
— Property::enumType 공급)·DrawEnumCombo(TypedDraw). 카나리아: 스코프드/
비스코프드/uint8_t 기저/값 간극·음수/무효값 빈 이름/NUL 종단. TypedYml은
enum을 int로 직렬화(레거시 파리티)라 magic_enum 무관 — 골든 영향 0.
리플렉션 사슬의 외부 라이브러리 의존은 yaml-cpp(직렬화 백엔드)만 남는다.

**존치 전수 감사(워크플로 4에이전트, 소비처 실측)** — 판정 요약:

| 판정 | 대상 |
|---|---|
| KEEP | Property::setter(콘솔 필드 설정) · Method/invoker+MakeMethod(DrawMethods 사슬) · Registry Find(name/ID)·GetAllTypeNames · Factory Create/Create<T>/CreateShared<T> · REFLECT_TYPE_LIST 3소비 · Meta::Find 2종 · UndoManager·CustomChangeCommand·UndoSystemInit/Final · ExtractTypeFromYAML(12소비)·DeserializePrefab · MetaYml 별칭 · ClassProperty.h(리플렉션 무관 실사용 4곳) |
| **DEAD** | Property::getter(유일 호출자 MakePropChangeCommand가 자체 사망) · 벡터 메타 6종(isVector·elementTypeInfo·createVectorIterator·elementTypeName·elementTypeID·isElementPointer — 등록 비용만 내고 독자 0) · offset · typeInfo(Property·MethodParameter 양쪽) · isPointer · VectorIteratorImpl/IVectorIterator · MakePropChangeCommand+PropertyChangeCommand<T> · **ReflectionYamlTemplete.h 파일 전체**(CT7 이후 자기참조만) · FactoryRegistry::CreateShared(size_t) 비템플릿 |
| REVIEW | MakePropertyImpl 벡터 특수 분기 3종(산출물 전부 DEAD — getter/setter/name/typeName/typeID만 남기면 분기 무의미) · MetaStateCommand.h 부분 사강(PropertyChangeCommand 블록만 국소 제거 타당) · ReflectionYml 미등록 폴백(레거시 실체 0 — 순수 방어 가드로 재분류) · MetaYml 별칭 3중 선언(DRY) |

정리 슬라이스(CT10 후보): DEAD 목록 일소 시 Property가 name·typeName·setter·
typeID·enumType·range/displayName 6필드 수준으로 수축하고 MakePropertyImpl
분기 3종이 접힌다 — 등록 시간·Property 크기 동반 감소 예상. 미착수(감사만).

### 원계획 CT7 (착수 전 설계 — 이행 기록 위해 보존)

- MetaGenerator pre-build 배선 제거(vcxproj 2곳) → generated.h 154개·헤더툴
  3종 바이너리·`[[Property]]`/`[[Serializable]]` 어노테이션·CT4 어댑터 삭제.
- `AutoRegisterCreateReflection` 프로젝트 저장소에서 제거.
- 최종 계측 보고(CT0 대비): 씬 로드·스폰 경로·인스펙터 프레임·빌드 시간.
- 문서 정산: 본 문서 완료 표기, 분석 문서 §7 갱신, RetentionDecision에 승계 기록.

## 4. 순서와 의존

```
CT0 ──→ CT1 ─┐            (CT1·CT2·CT3은 상호 독립, 병행 가능 —
      └─ CT2 ─┼─→ CT4 → CT5 → CT6 → CT7        단 CT2가 CT3·CT4의 표면을 줄인다)
      └─ CT3 ─┘
```

- **E5(shared_ptr 축소)·K2 잔여(SBO·unique_ptr)**: CT6과 같은 슬라이스 권장 —
  분리도 가능하지만 소유·컨테이너 전환의 회귀를 두 번 밟게 된다.
- **SerializationPlan D2(쿠킹)**: CT6 이후 착수가 이득 — typed 순회 위에 쿠킹을
  얹으면 any/function 상한 없이 시작한다. D0(직렬화 기준선)는 CT0과 겸용 가능.
- **E6(GameObject→Entity 리네임)**: 새 typeID가 이름 기반(FNV)이므로 여전히
  리네임 취약 — 디스크는 UUID(K1-b) 유지가 전제라는 층 분리를 CT4에서 깨지
  말 것. E6는 본 트랙과 독립.

## 5. 완료 기준

- 헤더툴 바이너리 0 · generated.h 0 · pre-build 스캔 배선 0.
- 리플렉션 경로의 `std::any`/`std::function` 사용 0 (브리지의 타입당 함수
  포인터 제외).
- 회귀 세트 그린(pwsh) + 전환 구간 골든 diff 0 유지 기록.
- CT0 대비 계측 보고: 씬 로드·`InstantiatePrefab`·인스펙터 프레임·빌드 시간.
- `[[Property]]`가 붙은 멤버의 컨테이너·소유 형태 제약(vector·shared 전제) 소멸
  — SBO 배열·unique_ptr 멤버가 반영 가능함을 검사로 증명(K2·E5 해제 조건).

## 6. 함정 (착수 전 필독)

1. **씬 typeID 검증 완화는 로그와 함께** — UUID 없는 구파일은 이름 검증이
   유일한 안전망이다. 완화 시 조용한 오식별이 생기지 않게 경고 로그 + 재저장
   유도(CT4).
2. **이중 정본 금지** — 헤더툴이 살아 있는 동안(CT7 전) generated.h를 손으로
   고치지 않는다. 타입 이전은 "메타 추가 + generated 블록 제거"를 한 커밋에.
3. **템플릿 전파 역행 주의** — typed 방문자(직렬화·인스펙터·쿠킹)를 헤더에
   풀어놓으면 CT3의 성과를 되물린다. 타입당 전용 TU 명시적 인스턴스화.
4. **NTTP 멤버명 추출은 컴파일러 버전 민감** — CT4의 static_assert 대조를
   유지하고, 툴체인 업그레이드 시 최우선 확인 지점으로 삼는다.
5. **회귀는 pwsh로** — 5.1은 한글 주석 인코딩 파손으로 거짓 실패.
6. **작업 중 사용자 동시 커밋** — 커밋 전 HEAD 재확인, `--update` 류 일괄
   흡수 금지(공유 워크트리 이력).
7. **자를 먼저 검증** — CT0 없이 CT1 착수 금지. 개선 주장은 전부 CT0 기준선
   대비 수치로 한다.
