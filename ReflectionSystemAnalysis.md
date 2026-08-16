# 네이티브 리플렉션 시스템 분석 — 구조 변경·성능 향상

2026-08-16. 근거: 소스 전수 실측(호출 지점 grep count · 코어 헤더 11종 정독 ·
등록 배선 추적). [ReflectionRetentionDecision.md](ReflectionRetentionDecision.md)(존치 결정)의
후속 문서다 — "존치한다"는 결론은 유지하되, **존치한 것을 어떻게 고칠 것인가**를
다룬다. [SerializationPlan.md](SerializationPlan.md)(PHASE 17)와
[SceneGraphRedesignPlan.md](SceneGraphRedesignPlan.md)(트랙 E·K)의 접점을 §5·§7에서 명시한다.

## 0. 요약

1. **런타임 매 프레임 비용은 없다. 그러나 게임플레이 이벤트당 비용은 있다** —
   몹 스폰·투사체 생성이 `InstantiatePrefab → Meta::Deserialize` 전체 왕복을 탄다(§2.1).
2. **에디터는 매 프레임 리플렉션을 문자열로 탄다** — 인스펙터가 열려 있는 동안
   문자열 맵 조회·`std::any` 박싱·벡터 전량 복사가 프레임마다 반복된다(§3.3).
3. **가장 큰 병은 성능이 아니라 구조다** — 리플렉션이 `std::vector`와 공유 소유를
   타입 계약 수준에서 하드코딩해서, 소유자 쪽 구조 개선(K2 SBO·unique_ptr)을
   역으로 봉쇄했다. 실측된 후퇴 2건이 그 증거다(§5).
4. **컴파일 파이프라인 전체가 리플렉션을 문다** — `Core.Minimal.h`가
   `Reflection.hpp`를 물어 yaml-cpp·magic_enum·`<any>`가 280개 TU에 전파되고,
   헤더 스코프 `static` 싱글턴 포인터 6종이 TU마다 사본으로 박힌다(§4.4).
5. 죽은 코드가 정본 자리를 차지하고 있다 — `consteval` 64비트 해시가 있는데
   `typeid().hash_code()` 32비트 절단을 쓰고, `InvokeMethodByMetaName`은 호출부
   0건, `ClassAutoRegistrar`는 인스턴스화 0건(§6).

## 1. 시스템 지도

### 1.1 코어 (Utility_Framework, 11파일 · 약 2,050줄 + 에디터 994줄)

| 파일 | 줄 | 역할 |
|---|---|---|
| `ReflectionType.h` | 113 | `Property`/`Method`/`Type`/`EnumType` 정의. `Type::properties`는 `std::span` — 실배열은 각 `Reflect()`의 함수-로컬 static |
| `ReflectionRegister.h` | 524 | 싱글턴 5종: `TypeCaster`(any↔void\*) · `Registry`(이름 맵+해시 맵) · `EnumRegistry` · `FactoryRegistry`(new/shared_alloc) · `UndoManager` |
| `ReflectionFunction.h` | 374 | `MakeProperty`/`MakeMethod`(람다 캡처 → `std::function`) · `Register<T>` · `Find` |
| `ReflectionMecro.h` | 134 | `ReflectionField`/`PropertyField`/`FieldEnd` 매크로 — 생성 헤더가 조립하는 부품 |
| `ReflectionYml.h` | 413 | `Serialize`/`Deserialize`/`DeserializePrefab`/`ExtractTypeFromYAML` |
| `ReflectionYamlTemplete.h` | 439 | 스칼라 직렬화 23종 + 벡터 17종 **배열 테이블**(선형 스캔) |
| `ReflectionVector{Factory,Invoker,Mapper}.h` | 112 | `vector<T>` 전용 팩토리·역직렬화 — **컨테이너 하드코딩의 실체**(§5.1) |
| `MetaAlias.h` | 22 | `GetterType = function<any(void*)>` 등 — **이중 타입소거를 별칭 수준에서 고정** |
| `MetaUtility.h` | 110 | `typeid().name()` 문자열 가공(`ToString`/`ExtractPointee` 등 substr 연산) |
| `TypeTrait.h` | 348 | `GUIDCreator::GetTypeID` = `typeid().hash_code()` **uint32 절단**. 미사용 `consteval MakeTypeID`(FNV-1a 64) 동거 |
| `EngineGUIWindow/ReflectionImGuiHelper.h` | 994 | 인스펙터 자동 그리기 — 프레임당 경로(§3.3) |

### 1.2 등록 흐름 (부팅 1회)

```
[pre-build] MetaGenerator.exe (정규식 스캐너, Utility_Framework·ScriptBinder vcxproj PreBuildEvent)
    → *.generated.h 154개 (2,547줄, 평균 16.5줄 — 매크로 블록)
    → ScriptBinder/RegisterReflect.def (AUTO_REGISTER_CLASS 76건)
[모듈 로드] AUTO_REGISTER_ENUM 31건 — static 전역, main 이전 등록
[부팅] SceneManager::ManagerInitialize() (EditorMain.cpp:190 · PlayerMain.cpp:133)
    → REFLECTION_REGISTER_EXECUTE() → Meta::Register<T>() ×76
```

등록 타입 총 107(클래스 76 + 열거형 31). 타입당 등록 비용: `Reflect()` 최초 호출로
함수-로컬 static `Property` 배열 구축(프로퍼티당 `std::function` 2개 + typeName
문자열), `Registry` 이름 맵·해시 맵에 `Type` **값 복사 2벌**(`ReflectionRegister.h:228-235`),
TypeCaster·FactoryRegistry·Vector 계열 등록. 1회성이라 총량은 작다 — 문제는
등록이 아니라 아래의 소비 경로다.

## 2. 실측 — 누가, 언제 부르는가

### 2.1 직렬화: 매 프레임은 없다, 이벤트당은 있다

`Meta::Serialize(` 18건(10파일) · `Meta::Deserialize(` 47건(12파일) ·
`Meta::DeserializePrefab(` 2건. 씬 로드/저장(`SceneManager.cpp` 8건), 프리팹
(`Prefab.cpp`·`PrefabUtility.cpp` 7건), 컴포넌트 로드(`ComponentFactory.cpp` 30건),
BT·블랙보드·설정·머테리얼 저장 등 — 게임 루프(`SceneManager.cpp:242-304`) 내부
직접 호출은 **0건**이다.

단, **게임플레이 이벤트 경유 반복 호출 경로**가 실재한다:

| 경로 | 트리거 | 근거 |
|---|---|---|
| `Object::Instantiate()` → Serialize+Deserialize 왕복 | 오브젝트 복제 API 호출마다 | `Object.cpp:124,126` |
| `Prefab::Instantiate()` → 오브젝트·컴포넌트마다 Deserialize | 프리팹 인스턴스화마다 | `Prefab.cpp:96,142` |
| `MobSpawner::Update` → 타이머 만료 시 `InstantiatePrefab` | **스폰마다(초당 다회 가능)** | `Dynamic_CPP/Assets/Script/MobSpawner.cpp:99,113,150` |
| `SpecialBullet::Update`/`OnTriggerEnter` → 폭발 이펙트 `InstantiatePrefab` | **충돌마다(전투 중 빈번)** | `SpecialBullet.cpp:28,129,161,170` |

즉 몹 하나 스폰할 때마다 YAML 트리 워크 + 프로퍼티당 문자열 맵 조회 + `std::any`
박싱 + `std::function` 간접호출로 구성된 §3.1 전체 비용이 지불된다. 애니메이션
스케줄러 계획(S 트랙)이 스폰 빈도를 실측했듯, 여기도 스폰 프로파일이 곧 리플렉션
프로파일이다.

### 2.2 조회: 문자열 17 : 해시 3

`Meta::Find(` 20건 중 이름 문자열 조회 17건(85%) — 그중 11건이
`InspectorWindow.cpp`(55,171,1115-1254,1599), 3건이 `ImGuiDrawHelperMeshRenderer.cpp`
— **에디터 매 프레임 문자열 해시 계산 + 맵 조회**다. typeID 조회는 3건뿐.
해시 맵(`Registry::hashMap`)이 이미 있는데 소비자가 안 쓴다.

### 2.3 메서드 호출: 죽은 API

`Meta::InvokeMethodByMetaName` 호출부 **0건**(정의 `ReflectionFunction.h:342`만 잔존).
[ReflectionRetentionDecision.md](ReflectionRetentionDecision.md):43의 "AnimationEventBridge·ActionMap이 쓴다"는
기술은 현재 소스와 불일치 — 두 파일 모두 `Meta::` 사용 0건(CoreCLR 레거시 은퇴에서
키프레임·입력 액션 전달 경로가 무해화된 결과로 추정). **`Method`/`Invoker`/
`MethodParameter` 체계 전체가 실소비자를 잃었다.** 유일한 UI 소비자는
`ReflectionImGuiHelper.h`의 `DrawMethods`(에디터 버튼)뿐이다.

### 2.4 프로퍼티 순회 소비자 5곳

| 소비자 | 빈도 |
|---|---|
| `ReflectionYml.h:100,281,394` (Serialize/Deserialize) | 로드·저장·스폰 이벤트당 |
| `ReflectionImGuiHelper.h:49` (`DrawProperties`) | **에디터 프레임당** × 열린 컴포넌트 × 프로퍼티 |
| `PrefabUtility.cpp:36` (`SeedTypeOverrides`) | 프리팹 갱신 시 1회 |

## 3. 성능 병폐 — 비용 집중 지점

### 3.1 직렬화 경로: primitive 하나에 조회 3단 폴백

`Serialize`/`Deserialize`는 프로퍼티마다 ① `MetaEnumRegistry->Find(typeName)`
문자열 맵 조회(실패) → ② `MetaDataRegistry->Find(typeName)` 문자열 맵 조회(실패) →
③ `FindYamlSerializer(typeID)` **23항목 배열 선형 스캔**(`ReflectionYamlTemplete.h:399-407`)
순으로 내려간다 — `int` 하나 쓰는 데도 매번(`ReflectionYml.h:176-191,344-356`).
`prop.getter`의 `std::any` 반환은 벡터 분기에서 **결과를 쓰지 않고 버려진다**
(`ReflectionYml.h:102` → 105-151 미사용). 상속 단계마다 부모 노드 키 단위
재복사(깊이 N이면 N번 복사, `ReflectionYml.h:91-97`)는 SerializationPlan §1.3에서
이미 지적된 그대로다.

### 3.2 접근 계약: 이중 타입소거 + 값 복사

`GetterType = std::function<std::any(void*)>`(`MetaAlias.h:13-15`) — 모든 프로퍼티
접근이 (a) `std::function` 간접호출, (b) `std::any` 박싱(값 **복사**), 두 겹을
강제로 통과한다. `std::string`·`Vector4`급은 any의 SBO(MSVC 16B)를 넘어 **접근
1회당 힙 할당 1회**가 추가된다. `Property`에 `offset`이 이미 있는데도
(구조체 중첩 경로만 offset을 쓴다, `ReflectionYml.h:185,346`) 스칼라 경로는 전부
getter/setter 복사 왕복이다.

### 3.3 인스펙터: 매 프레임 반복되는 것들 (에디터 프레임당 × 인스턴스 × 프로퍼티)

| 병폐 | 근거 |
|---|---|
| 타입 디스패치가 해시 스위치가 아니라 **정수 비교 선형 if-else 체인 25+분기** | `ReflectionImGuiHelper.h:47-837` |
| `prop.name == "m_isEnabled"` — `const char*` **포인터 비교**. 리터럴 풀링에 우연히 의존 | `ReflectionImGuiHelper.h:51` |
| 정수 체인 중간에 `std::string` 비교 혼재(`"UINT"`·`"bool32"`) | `:66,99` |
| 벡터 프로퍼티: CollapsingHeader가 **접혀 있어도** 가상 이터레이터로 전 원소를 임시 벡터에 매 프레임 복사 | `:136-419` |
| 포인터 프로퍼티마다 매 프레임 `ExtractPointee`+`RemoveObjectPrefix` — substr로 새 문자열 생성 | `:699` |
| `DrawMethods` 파라미터 키를 매 프레임 문자열 연결로 생성, static 맵을 전 컴포넌트가 공유 | `:842,870,929` |
| `vector<int>` 분기가 동일 조건으로 **두 번** — 두 번째는 도달 불가 죽은 분기 | `:246,304` |
| 프레임당 `MetaDataRegistry->Find(문자열)` — §2.2의 11+3건 | `InspectorWindow.cpp` 외 |

### 3.4 컴파일 타임: 전 TU 전파

- `Core.Minimal.h:9`가 `Reflection.hpp`를 include → **280개 파일**이 Core.Minimal을
  include → 리플렉션 사슬(`ReflectionRegister.h`의 **yaml-cpp**, `ReflectionFunction.h`의
  **magic_enum**, `<any>`/`<functional>`/`<typeindex>`)이 사실상 전 TU에 전파된다.
  직렬화를 안 하는 TU도 yaml-cpp를 파싱한다.
- 헤더 네임스페이스 스코프 `static auto` 싱글턴 포인터 **6종**(`TypeCast`·
  `MetaDataRegistry`·`MetaEnumRegistry`·`MetaFactoryRegistry`·`UndoCommandManager`·
  `VectorFactory` — `ReflectionRegister.h:216,289,316,396,484` ·
  `ReflectionVectorFactory.h:37`)이 내부 링크라 **TU마다 사본 + TU마다 정적 초기화
  `GetInstance()` 호출**이 박힌다. 280 TU × 6 = 최대 1,680개의 정적 초기화 항목.
- pre-build마다 MetaGenerator가 저장소를 재귀 2회 순회하며 `.generated.h` 154개를
  재생성하고 **원본 헤더를 in-place 재작성**한다(`AutoRegisterCreateReflection.cpp:152-197`)
  — 내용이 같아도 타임스탬프 갱신 시 증분 빌드를 오염시킬 수 있는 구조.
- `FieldEnd` 매크로의 `std::string type_name = #T;`(`ReflectionMecro.h:77-79`)는
  매직 스태틱 가드 **밖** — `Reflect()` 호출마다 무용한 문자열 생성(15자 초과
  클래스명은 힙 할당). 호출 빈도가 낮아 실해는 작지만, 생성 코드의 품질 문제다.

## 4. 정확성·안전 결함

| # | 심각도 | 내용 | 근거 |
|---|---|---|---|
| F-1 | HIGH | **typeID = `typeid().hash_code()`의 uint32 절단** — 런타임 RTTI 의존, 모듈 간 동일성 표준 미보장(전 싱글턴이 DLL 경계 공유 구조), 타입 증가 시 충돌 확률 상승. 같은 파일의 `consteval MakeTypeID`(FNV-1a 64, 결정적)는 호출부 0 | `TypeTrait.h:199-204` vs `:17-24,188-194` |
| F-2 | HIGH | **원시 포인터 프로퍼티 재역직렬화 누수** — 항상 새 인스턴스를 만들어 setter로 덮고, 기존 포인티는 해제하지 않는다(shared_ptr 프로퍼티만 대입으로 해제됨) | `ReflectionYml.h:299-311` |
| F-3 | MEDIUM | **`MakeAnyFromRaw`가 무조건 새 소유 shared_ptr 생성**(`std::shared_ptr<T>(raw)`, 기본 deleter) — 이미 소유자가 있는 포인터에 쓰이면 이중 해제, `shared_alloc`(ManagedHeap)으로 만든 객체에 쓰이면 할당자 불일치. 현 호출처(역직렬화 신규 생성 직후)는 안전하나 계약이 위험을 내장 | `ReflectionRegister.h:76-79` |
| F-4 | MEDIUM | `Registry`가 이름 맵·해시 맵에 `Type`을 **값 복사 2벌** 저장 — 메모리 2배는 사소하나 `UnRegister`가 두 맵을 따로 지워야 해 탈동기화 여지(스크립트 핫리로드 경로가 실사용) | `ReflectionRegister.h:226-237,255-270` |
| F-5 | MEDIUM | 인스펙터 `prop.name` 포인터 비교 — 리터럴 풀링 꺼지면(/GF 미적용 TU·DLL 경계) 조용히 오동작 | `ReflectionImGuiHelper.h:51` |
| F-6 | LOW | 죽은 코드 4종: `InvokeMethodByMetaName`(호출 0) · `ClassAutoRegistrar`(인스턴스화 0) · `consteval MakeTypeID`/`fnv1a_64`(호출 0) · 인스펙터 `vector<int>` 중복 분기 | §2.3 · `ReflectionRegister.h:497` · `TypeTrait.h` · `ReflectionImGuiHelper.h:304` |
| F-7 | LOW | [ReflectionRetentionDecision.md](ReflectionRetentionDecision.md):43의 문자열 디스패치 존치 근거가 소스와 불일치(스테일) — 존치 결론 자체는 여전히 유효(자산 포맷·핫리로드·인스펙터 3근거 건재) | `AnimationEventBridge.cpp`·`ActionMap.cpp` Meta 사용 0건 |

## 5. 구조 병폐 — K2 후퇴 2건이 실증한 결합

SceneGraphRedesignPlan K2(컴포넌트 컨테이너 이중 구조 소멸, `b08ec7e4`)는 SBO와
unique_ptr 전환을 **리플렉션 때문에** 후퇴시켰다. 이는 개별 사건이 아니라
리플렉션 계약의 구조적 병폐 두 가지가 소유자 코드의 진화를 봉쇄한 사례다.

### 5.1 병폐 A — 컨테이너가 계약에 하드코딩됨 (`std::vector` 전제)

리플렉션은 "순회 가능한 시퀀스"를 추상화하지 않았다. `std::vector<T>` 구체 타입이
네 겹으로 박혀 있다:

1. `VectorIteratorImpl<T>`가 `std::vector<T>::iterator`를 직접 소유 — `ReflectionType.h:15-39`
2. `is_vector_v` 판별 + `VectorElementType` 추출이 vector 특수화 — `ReflectionFunction.h:82,100-128`
3. `VectorFactoryRegistry`가 `new std::vector<T>()`를 람다로 캡처 — `ReflectionVectorFactory.h:18-25`
4. 역직렬화가 `fromYamlVector(instance, prop.offset, node)`로 **인스턴스 메모리의
   해당 오프셋에 vector가 있다고 전제**하고 직접 채운다 — `ReflectionYml.h:315-341`,
   `ReflectionVectorMapper.h:11-30`

결과: `[[Property]]`가 붙은 멤버는 `std::vector`여야만 한다. K2가 컴포넌트
컨테이너를 SBO 배열(소형 버퍼 + uint64 타입마스크)로 바꾸려면 ①~④를 전부
같이 바꿔야 하므로 후퇴했다(SceneGraphRedesignPlan §K2, "[[Property]] 표면 유지").
**메타시스템이 소유자의 데이터 표현을 결정하는 추상화 역전**이다 — 리플렉션은
표현을 관찰해야지, 강제해서는 안 된다.

### 5.2 병폐 B — 소유 모델이 계약에 하드코딩됨 (공유 소유 전제)

`TypeCaster`의 변환 어휘는 값·원시 포인터·**shared_ptr** 세 가지뿐이다
(`ReflectionRegister.h:29-82`). `unique_ptr`는 표현 자체가 없다 — `std::any`에
넣으려면 복사 가능해야 하는데 unique_ptr는 이동 전용이라, **`any` 기반 계약과
원리적으로 충돌**한다(any를 쓰는 한 unique_ptr 프로퍼티는 불가능). 여기에:

- `FactoryRegistry::Register<T>`가 `Managed::HeapObject` 파생이면 `shared_alloc<T>`
  (공유 소유 전제 ManagedHeap API)를 등록 — `ReflectionRegister.h:328-350`
- `MakeAnyFromRaw`는 raw에서 새 shared_ptr을 만든다 — `:76-79`
- `Property::isPointer`는 raw와 shared_ptr을 한 플래그로 뭉뚱그린다 — `ReflectionType.h:50`

결과: K2의 컴포넌트 `unique_ptr` 소유 전환은 할당이 `shared_alloc/CreateShared`를
경유하는 한 불가능해 감사 전 보류됐고, E5(shared_ptr 축소 — 소유는 슬롯
`unique_ptr` 하나, 참조 99곳/23파일을 핸들로)도 같은 벽을 만난다. **엔진의 소유권
정리는 리플렉션 접근 계약을 재설계하지 않으면 완주할 수 없다.**

### 5.3 공통 뿌리

두 병폐의 뿌리는 하나다: `Property`가 "**구체 타입을 아는 값 복사 계약**"
(getter가 `T`를 any로 복사, vector 이터레이터가 구체 iterator 소유)이지,
"**레이아웃 접근 계약**"(offset + 크기 + 타입 태그 + 시퀀스 어댑터)이 아니라는 것.
전자는 멤버의 실제 타입이 바뀌면 계약이 깨지고, 후자는 어댑터만 갈아끼우면 된다.
§7의 RF3이 이 뿌리를 겨눈다.

## 6. 기존 결정과의 관계

- **존치 결정은 유지** — 자산 포맷 종속·핫리로드 상태 보존·인스펙터 자동 생성
  3근거는 여전히 실측대로다. 단 부수 근거(문자열 메서드 디스패치)는 소멸했다(F-7)
  — 오히려 `Method` 체계 축소의 근거가 된다.
- **R2(ComponentFactory 수동 분기)** 는 실측이 정밀화됐다: 31건이 아니라
  **`type_guid` 분기 17개 + `Meta::Deserialize` 호출 30건**이며, 각 분기는 단순
  중복이 아니라 **애셋 로드 후처리(GUID→애셋 해석)가 리플렉션 밖에 하드코딩된
  하이브리드**다(`ComponentFactory.cpp:111-674`). "레지스트리 디스패치로 전환"만으론
  안 되고, 후처리를 프로퍼티 어노테이션(예: 애셋 참조 타입)으로 데이터화해야
  분기가 사라진다.
- **SerializationPlan(PHASE 17) 과의 접점**: 쿠킹(D-track)은 "리플렉션 순회 →
  바이너리"이므로 §3.1의 조회 3단 폴백과 §3.2의 이중 타입소거가 **쿠킹 성능의
  상한**이 된다. 포맷을 바꿔도 순회 계약이 그대로면 비용이 따라간다(같은 문서
  §1.3의 결론과 일치). RF3은 D2(쿠킹 순회) 전에 앞당길 가치가 있다.

## 7. 개선 트랙 제안

> **승계(2026-08-16)**: 본 절의 RF 트랙과 §8의 전환 평가는
> [ReflectionRedesignPlan.md](ReflectionRedesignPlan.md)(**PHASE 18**, 슬라이스 CT0~CT7)로 실행
> 계획화됐다. 착수 순서·검증 기준은 그 문서가 정본이다.
>
> **완결(2026-08-17)**: CT0~CT7 전부 이행 완료. 본 문서의 병폐 목록은 이제
> 역사 기록이다 — any/function 이중 소거·vector 하드코딩·헤더툴 파이프라인·
> typeid 절단·전 TU 전파 모두 소멸했고, 표기는 매크로 프리 P2996 유사
> 표면(consteval describe + meta::member/method/attribute) 한 벌이다.
> 최종 수치: 무변경 빌드 1m29s→7.1s(12.6×), 스폰 경로 3.0~3.8→1.37ms(~2.5×),
> generated.h 154→0. 남은 미래 항목은 계획 문서의 존치 기록(어댑터 =
> 이름 기반 소비자의 정본 테이블)과 P2996 착지 시의 표기 접힘뿐이다.

우선순위는 (위험 대비 회수) 순. RF1·RF2는 독립 슬라이스, RF3는 E5·K2 잔여와
한 몸, RF4는 빌드 위생, RF5·RF6은 기존 부채(R1·R2) 승계다.

### RF1 — 즉효 저위험 (에디터 체감 + 코드 위생)

1. 인스펙터 `Meta::Find(문자열)` 14건 → typeID 조회로(해시 맵은 이미 있다).
2. `FindYamlSerializer`/`FindYamlVectorEntry` 배열 선형 스캔 → typeID 키 해시 맵.
3. 벡터 편집 UI: CollapsingHeader 열림 검사를 복사 **앞**으로 — 접힌 벡터의
   프레임당 전량 복사 제거(`ReflectionImGuiHelper.h:136-419`).
4. `prop.name` 포인터 비교 → `strcmp` 또는 프로퍼티 플래그(F-5).
5. `FieldEnd`의 type_name을 매직 스태틱 안으로(생성기 수정 1곳).
6. 죽은 코드 4종 제거(F-6) + ReflectionRetentionDecision.md:43 스테일 기술 정정.
   **ScriptReflect 갈래 은퇴 포함** — ModuleBehavior 사망(§8.2)으로 매크로 6건·
   ScriptReflectionHeaderTool 2종이 무소비 상태. R3(위험 최상)가 삭제로 강등된다.
7. 직렬화 조회 순서 역전: `FindYamlSerializer(typeID)` **먼저**(스칼라가 다수),
   실패 시에만 struct/enum 문자열 조회 — 폴백 사슬을 다수 케이스에 맞춘다.

### RF2 — typeID 정본 교체 (F-1)

죽은 `consteval MakeTypeID`(FNV-1a 64, 이름 기반 결정적)를 채택해
`typeid().hash_code()` 절단을 은퇴시킨다. 주의 둘:
- 디스크의 이름해시(씬 yaml `타입명: typeID` 헤더)와 얽힌다 — K1-b 영속 UUID가
  이미 디스크 정본을 인수했으므로, 런타임 해시 교체의 창이 열려 있다.
  SerializationPlan의 "호환 무시" 전제가 유일한 일괄 전환 기회라는 판단(같은 문서
  §1.5)과 같은 창이다.
- FNV-1a도 이름 기반이라 리네임 취약성은 동일 — 런타임 식별자로만 쓰고, 디스크는
  UUID(K1-b)라는 층 분리(SceneGraphRedesignPlan §3 원칙 6)를 유지한다.

### RF3 — 접근 계약 재설계 (병폐 A·B의 뿌리, E5·K2 잔여의 선행 조건)

`Property`를 "값 복사 getter/setter + 구체 컨테이너 지식"에서 "레이아웃 접근"으로:

- 스칼라·POD: `offset` + 타입 태그로 **직접 접근**(이미 offset을 갖고 있다).
  `std::any` 박싱은 폴백으로 강등.
- 시퀀스: `VectorIteratorFunc` 대신 **시퀀스 어댑터 인터페이스**(begin/size/
  element-at/append — 컨테이너 무지). vector·SBO 배열·span이 각자 어댑터 제공 →
  병폐 A 해소, K2 SBO 재개 가능.
- 소유: `isPointer` 한 플래그를 소유 종별(값/관찰 포인터/unique/shared)로 분해.
  unique_ptr는 any 경유가 원리적으로 불가하므로 **역직렬화 생성 경로를
  "팩토리 생성 → 소유 이전 콜백"으로**(any를 통과하지 않는 setter 변형) → 병폐 B
  해소, E5와 같은 슬라이스로.
- `Method` 체계는 실소비자(에디터 버튼)만 남기고 축소 또는 제거(§2.3).

폭발 반경: 생성기(`AutoRegisterCreateReflection.cpp`) + `MakePropertyImpl` +
`ReflectionYml.h` + `ReflectionImGuiHelper.h` + 컨슈머 5곳. 씬 로드 회귀
(`Tools/regression`, pwsh) 필수. **E5(shared_ptr 축소)와 반드시 같은 트랙으로** —
따로 하면 서로가 서로의 족쇄다.

### RF4 — 헤더 전파 절단 (빌드 위생)

1. `Core.Minimal.h`에서 `Reflection.hpp` 분리 — 리플렉션이 필요한 TU만 물게
   (UtilityFrameworkModernizationPlan의 전이 include 실측과 같은 수법).
2. 헤더 스코프 `static auto` 싱글턴 6종 → `inline` 함수 반환 또는 사용부 직접
   `GetInstance()` — TU당 정적 초기화 사본 제거.
3. `ReflectionRegister.h`의 yaml-cpp include 제거(직렬화 전용 헤더로 격리) —
   UndoManager·MetaStateCommand는 리플렉션 코어와 무관하므로 분리.
4. MetaGenerator: 출력이 기존과 동일하면 파일을 다시 쓰지 않게(타임스탬프 보존) —
   증분 빌드 보호.

### RF5 — ComponentFactory 데이터화 (R2 승계, 정밀화)

애셋 참조 후처리를 프로퍼티 메타데이터(애셋 참조 타입 태그)로 옮겨 17분기를
공통 경로에 흡수. Animator처럼 리플렉션 진입점이 8곳인 컴포넌트가 우선 검증 대상.
씬 로드 회귀 필수.

### RF6 — 인스펙터 재배치 + 테이블화 (R1 승계)

`ReflectionImGuiHelper.h`는 이미 EngineGUIWindow로 이관 완료(R1 해소 확인).
남은 것은 선형 if-else 체인 → typeID 키 드로어 테이블, `DrawMethods` 키 캐싱.
RF1-3·4와 같은 파일이므로 한 슬라이스로 묶는다.

### 순서 제안

```
RF1(즉효) → RF4(빌드 위생, 독립) → RF2(typeID, K1-b 창구 활용)
    → RF3+E5(접근 계약 + 소유 정리, 한 트랙) → RF5(팩토리 데이터화)
    → SerializationPlan D2(쿠킹)는 RF3 이후가 이득
```

## 8. 컴파일타임 리플렉션 전환 평가

질문: 헤더툴(외부 코드 분석 + 생성기) 주입을 버리고, qlibs/reflect **와 유사한
설계**(라이브러리 채택이 아니라 컴파일타임·언어내 리플렉션으로의 재작성)로
전환하면 병폐가 제거되는가.

### 8.1 전제 실측

qlibs/reflect: C++20 · **MSVC 19.36+ 지원**(현 VS 18 툴체인 충족) · 구조적
바인딩 + `source_location` 기법 · 멤버 기본 64개(확장 가능) · P2996(C++26 표준
리플렉션)의 원시 기능 부분집합을 선제 제공하는 브리지 성격.

구조적 바인딩 **기법**의 제약: 집합체(aggregate)만 분해된다 — 가상 함수·
사용자 선언 생성자·private 멤버·기반 클래스 멤버 혼합 불가. 단 이것은
**기법의 제약이지 컴파일타임 설계의 본질이 아니다**(§8.3에서 우회).

### 8.2 갈래 정리 — 구지원 장애물은 실재하지 않는다

당초 비집합체 원인으로 꼽을 뻔한 두 가지는 실측 결과 **은퇴 대상 잔재**다:

- **`ScriptReflect()` 갈래는 ModuleBehavior(네이티브 C++ 스크립트)의 잔재가
  맞다.** C++ 스크립트 은퇴(9-4)로 ScriptBinder에 `class ModuleBehavior` 정의
  0건, HotLoadSystem 0건 — 남은 것은 구포맷 씬 로드 가드
  (`ComponentFactory.cpp:76-81`, InvalidScriptComponent 대체)와
  `ReflectionMecro.h`의 매크로 6건, 그리고 빌드에서 빠진 `Dynamic_CPP` 스크립트
  헤더들(데이터 보존 폴더)뿐이다. **살릴 이유가 없다** — R3("헤더툴 통합, 위험
  최상")는 "한쪽 갈래 삭제"로 강등되고, ScriptReflectionHeaderTool 2종 바이너리도
  함께 은퇴한다.
- `GENERATED_BODY(T)`의 생성자 선언도 구지원 의무가 없다 — 재설계 시 유지할
  이유가 없는 관례다.

**그러나 집합체 제약의 본체는 따로 있다**: Component의 가상 생명주기 14개
(`Component.h:31-54` — Awake/Update/OnEnable… + OnInitialized 계열)와
`shared_ptr<Component>` 다형 저장이다. 이것은 잔재가 아니라 현행 실행 모델의
정본이므로, "잔재 제거"만으로는 컴포넌트가 집합체가 되지 않는다. (트랙 L이
완성되어 스크립팅이 C#으로, 네이티브 컴포넌트가 시스템이 소비하는 데이터로
이동하면 이 가상 표면 자체가 줄어들지만 — 그건 §8.4의 종착지 이야기다.)

### 8.3 핵심 정정 — 기법을 바꾸면 수술 없이 성립한다

"유사 설계"의 목표를 정확히 잡으면: **멤버를 실제 타입으로 보는 컴파일타임
방문(typed visitation)**이지, 구조적 바인딩이라는 특정 기법이 아니다. 자작
설계라면 기법을 골라 쓸 수 있고, 집합체 제약이 없는 기법이 있다:

**멤버 포인터 튜플 기반 명시 메타** (선례: glaze — MSVC 포함 실전 검증):

```cpp
struct MeshRenderer : Component {
    // ... 가상 생명주기·private 멤버 그대로 ...
    static constexpr auto reflect = Meta::members(
        &MeshRenderer::m_Material, &MeshRenderer::m_Mesh, /* ... */);
};
// 소비: 컴파일타임 for_each — 멤버를 실제 타입 T&로 방문
Meta::for_each(obj, [&](std::string_view name, auto& member) { /* ... */ });
```

- 멤버 포인터는 **가상 함수·상속·private(클래스 내 선언 시) 무관** — 현행
  다형 컴포넌트 154타입에 **그대로** 적용된다. 집합체 수술 불필요.
- 멤버 이름은 NTTP `source_location` 추출(MSVC 동작, glaze 방식) 또는 매크로
  문자열화 — 현행 `#member`와 동일한 이름이 나오므로 **YAML 필드명 유지 =
  자산 호환 유지**.
- 상속은 `Meta::members`에 부모 튜플 연결(concat)로 — 현행 `parent` 체인의
  키 단위 노드 재복사(§3.1)도 함께 사라진다.
- `[[Property]]` 선택 반영 의미론도 그대로다 — 목록에 적은 멤버만 반영.
  현행은 "어노테이션 + 생성 헤더" 이중 표기였는데, 이제 표기가 한 곳이다.

이 기법으로 §5의 병폐가 뿌리에서 소멸한다: 방문자가 실제 타입을 보므로
any 박싱·function 간접호출이 없고(§3.2), 컨테이너는 제네릭 레인지로(병폐 A —
K2 SBO 재개), unique_ptr 표현 가능(병폐 B — E5 족쇄 해제), typeID는 컴파일타임
결정적 해시로(F-1), 헤더툴 3종·generated.h 154개·pre-build 정규식 스캔·원본
in-place 재작성 전부 은퇴.

**이중 경로 설계**가 자연스럽다: ① 진짜 집합체(Mathf 계열·설정 구조체 등)는
PFR/reflect식 **자동 반영**(표기 0), ② 다형 컴포넌트는 위의 **명시 메타**.
P2996이 오면 ②의 표기가 자동으로 접히는 방향이다.

### 8.4 유지해야 하는 것 — 컴파일타임이 대체 못 하는 층

- **런타임 레지스트리는 남는다.** 씬 로드의 문자열/UUID → 타입 디스패치를 위해
  타입별 방문자를 부팅 시 등록하는 브리지(타입당 함수 포인터 수 개 — 현행
  프로퍼티당 `std::function` 2개보다 얇다)는 유지된다. C# 쪽 상태 왕복도 이
  브리지를 쓴다.
- **인스펙터도 브리지 경유** — 타입당 `Draw(void*)` 하나가 컴파일타임 방문을
  인스턴스화. 디스패치가 프로퍼티당 선형 체인에서 타입당 1회 간접호출로 준다.
- **컴파일타임 비용 주의**: 방문자 템플릿(직렬화·인스펙터·쿠킹 각각)이 TU마다
  인스턴스화되면 RF4(전파 절단)와 역행한다. 타입당 전용 TU 명시적 인스턴스화로
  통제할 것.
- **상태/캐시 분리(Data 집합체)는 이제 선택지다** — 명시 메타 덕에 선행 조건이
  아니게 됐지만, E5·K2·트랙 L(저작/실행 분리)이 진행되면 ①의 자동 반영 경로로
  자연 승격되는 종착지로 남는다.

### 8.5 대안 비교와 권고

| 경로 | 헤더툴 제거 | 병폐 A·B 제거 | 컴포넌트 수술 | 위험 |
|---|---|---|---|---|
| (가) 생성기만 교체(regex→libclang 등) | 툴 개선일 뿐 잔존 | ✗ | 불필요 | 낮음, 회수도 낮음 |
| (나) variadic 매크로로 **현 계약** 조립(툴만 제거) | ✓ (즉시) | ✗ | 불필요 | 낮음 |
| (다) **명시 메타(멤버 포인터) + 컴파일타임 방문** — 자작, glaze식 | ✓ | **✓ (뿌리)** | **불필요** | **중간** — 소비자 5곳 재작성, 회귀 필수 |
| (라) 집합체 분리 + 자동 반영(PFR/reflect식) | ✓ | ✓ (뿌리) | 전 154타입 | 높음 — E·K 완성 후의 종착지 |
| (마) P2996 대기 | — | — | — | MSVC 미탑재, 시기 미정 |

**권고: (다)를 RF3의 구현 형태로 채택한다.** 당초 평가에서 (라)만 보고 "E·K와
한 몸일 때만 성립"이라 했으나, ScriptReflect 잔재 확정(§8.2)과 명시 메타
기법(§8.3)으로 **컴포넌트 수술 없이 성립**함이 확인됐다 — 위험이 한 단계
내려가고 E5·K2와 분리 가능해진다(시너지는 유지: 같은 슬라이스면 소유·컨테이너
전환을 한 번의 회귀로 검증). 순서: RF1·RF4 선행 → ScriptReflect 갈래 삭제(R3
강등분, 소거로 표면 축소) → (다)로 RF3 — 집합체 타입 자동 경로 파일럿 →
컴포넌트 명시 메타 본대 전환 → 헤더툴 3종 은퇴. (라)는 트랙 L·E 완성 후
자연 승격, (마)가 오면 표기만 접힌다.

## 9. 함정 기록

- **자를 먼저 검증한다** — 본 문서의 프레임당/이벤트당 구분은 grep 기반 정적
  추적이다. RF1 착수 전에 인스펙터 열림 상태의 프로파일 캡처(PHASE 14 P0 하네스)로
  §3.3 비용을 수치화할 것.
- **생성기와 소스의 이중 정본** — `.generated.h`를 손으로 고치면 다음 pre-build가
  덮어쓴다. RF3의 계약 변경은 반드시 생성기부터.
- **핫리로드 경계** — `ScriptRegister`/`UnRegister`(이름·해시 맵 2벌 동기화, F-4)는
  스크립트 DLL 교체가 실사용 경로다. RF3에서 Registry 구조를 바꿀 때 핫리로드
  왕복(Serialize→재로드→Deserialize) 회귀를 함께 밟아야 한다.
- **문서 근거의 부패** — F-7처럼 결정 문서의 근거가 소스보다 늦게 늙는다. 큰 결정
  전 근거 재실측은 이번에도 유효했다.
