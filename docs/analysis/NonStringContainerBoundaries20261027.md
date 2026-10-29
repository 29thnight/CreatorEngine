# 문자열 제외 컨테이너 잔여 경계 분류

현재 `master`의 작업 트리(HEAD `27394992c`)를 추적·미추적 소스까지 검사했다. 문자열 타입 변경은 보류하고, 문자열을 키·값으로 담는 컨테이너는 내부 전환 대상에 포함했다. 이번 작업은 분류와 근거 정리이며 엔진 구현·성능 합격 정본은 변경하지 않았다.

## 집계와 범위

| 구분 | 타입 참조 수 | 의미 |
|---|---:|---|
| 저장 컨테이너 | **753** | 236개 파일, 추적 소스 678건·미추적 소스 75건 |
| 보류한 문자열 타입 | 8,379 | `string` 8,107·`wstring` 240·`u8string` 32 |
| 별도 함수 래퍼 | 264 | `std::function`; 캡처 소유권·호출자 수명·용량 정책 검토 대상 |
| 기존 감사 합계 | **9,396** | 위 세 구분 합계 |

753건은 독립 객체나 작업 753개가 아니라 **타입 토큰 참조**다. 선언·함수 인수·반환·중첩 타입·테스트 표현이 각각 포함된다. 예를 들어 Undo의 스택 참조 6건은 실제 멤버 4곳과 전송 함수 인수 2곳이다. 조건부 컴파일 분기도 포함하며 매크로 전개·템플릿 인스턴스별 횟수는 세지 않았다.

범위는 `Engine`, `Editor`, `Lattice`, `Player`, `HeadlessHost`의 C++ 소스 1,496개(추적 1,480·미추적 16)다. `Tools`, `ThirdParty`, 빌드 생성물과 `Engine/Containers` 정본은 잔여량 집계에서 제외했다. 주석·문자열 리터럴을 제외하는 기존 어휘 감사를 재사용했다.

기존 감사 밖의 보조 검사에서는 `std::span` 1,448건과 문자열 뷰 1,641건(`string_view` 1,611·`wstring_view` 30)을 찾았다. 전자는 비소유 뷰 API 경로로 별도 관리하고 후자는 문자열 보류에 포함한다. 이번 저장 컨테이너 753건에 합산하지 않았으며 면제 또는 전환 완료를 뜻하지 않는다. 직접 표기한 `concurrency::concurrent_*`와 `using std::<감사대상 타입>`은 이 범위에서 발견되지 않았다. 간접 별칭·매크로까지 없다는 증명은 아니다.

## 전환 경계

| 분류 | 참조 | 파일 수 | 현재 대응과 다음 조건 |
|---|---:|---:|---|
| 값 배열·비트셋 일괄 전환 후보 | **61** | 8 | `ce::dynamic_array`, `ce::fixed_array`, `ce::fixed_bitset`; 내부 인수·반환·소비자와 함께 변경 |
| 해시 맵 계약 검토 | **201** | 93 | `ce::node_hash_map` 등 활용 가능. 키 해시·동등성·API·반복 순서·노드 해제 정책 확인 |
| 해시 집합 API·수명 검토 | **92** | 38 | `ce::flat_hash_set`은 STL 집합과 반환값·반복기·주소 안정성·노드 저장소 정책이 다름. 일괄 이름 치환 금지 |
| 정렬·범위 연산 대체 설계 | **355** | 114 | map 235·set 118·multimap 1·multiset 1. 현재 정본에 직접 대응하는 정렬 컨테이너가 없음 |
| deque 성장·주소·수명 설계 | **34** | 26 | 일반 deque 대응 구현이 없음. 무제한 성장·양 끝 삭제·주소 안정성 검토 |
| 어댑터 계약 검토 | **8** | 3 | stack 6·queue 1·priority_queue 1. 기존 값 저장소 활용 가능성은 있지만 별도 연산·순서·할당 계약 필요 |
| 확인된 외부 ABI 경계 | **2** | 1 | reflgen 바이트 반환 인터페이스 1곳의 반환 타입과 결과 버퍼 |
| 합계 | **753** | **236개 고유 파일** | 분류별 파일 수는 중복되므로 합산하지 않음 |

이 표는 구현 경로 분류다. 61건도 컴파일·수명 검증이 끝난 자동 치환 허가 목록은 아니다. JSON/CSV의 추가 표시(헤더·모듈, 포인터·소유자, 문자열 연관, 수명 관련 영역)는 보수적인 검토 힌트이며 모든 호출자의 의미를 판정한 결과가 아니다. 문자열 연관 표시는 드러난 템플릿 인수와 일부 알려진 별칭만 검사하므로 별칭을 통한 모든 문자열 의존을 포괄하지 않는다.

## 확인한 경계와 보존할 계약

### 먼저 묶을 수 있는 값 배열·비트셋

61건 중 **59건은 Effect 계열의 미추적 파일 6개**에 있고, 나머지 2건은 기존 비트셋 사용처다. Effect의 Definition/ModuleIR/State와 RenderEngine의 EffectFrame은 값 모델·함수 시그니처를 함께 바꾸는 배치로 잡는다. 현재 사용자의 미추적 작업이라는 상태를 보존하고, 관련 테스트·직렬화 소비자를 함께 점검해야 한다. 문자열인 `DefinitionId` 자체는 유지한다.

- [EffectDefinition.h](../../Engine/Effect/EffectDefinition.h): Curve/Gradient·모듈 입력·Emitter 값 배열과 공개 함수 인수·반환.
- [EffectModuleIR.h](../../Engine/Effect/EffectModuleIR.h): 중첩 배열·명령·진단 결과·함수 인터페이스. 같은 줄의 중첩 vector도 각각 집계된다.
- [EffectFrame.h](../../Engine/RenderEngine/Effects/EffectFrame.h): 고정 행렬 배열과 프레임 값 배열.
- [DynamicBitset 모듈](../../Engine/Contracts/Core/Creator.Core.DynamicBitset.ixx): 64비트 블록의 `std::bitset` 사용. 생성·비트 연산·마지막 블록 마스킹 계약을 유지한다.
- [RHISoftwareRecorder.cpp](../../Engine/RenderEngine/RHI/RHISoftwareRecorder.cpp): 8비트 중복 검사.

### 해시 계열은 내부 전환 대상

`node_hash_map`은 값 주소를 유지하는 경로에 사용할 수 있고, 삭제 시 노드 반환이 필요한 사용처에는 `PoolNodes=false`를 명시한다. `flat_hash_set`의 `insert`는 bool, `find`는 포인터를 반환하며 STL 반복기 계약과 같지 않다. 집합을 flat 저장소로 바꾸면 rehash 시 참조 안정성과 노드 저장소 반환 특성이 달라지므로 사용처별 어댑터 또는 별도 노드 집합 설계가 필요하다.

- [FbxImporter.cpp:232](../../Engine/RenderEngine/Experiment/Import/FbxImporter.cpp): 로컬 코너→인덱스 맵은 ufbx의 함수 인수가 아니다. SDK 관련 파일이라는 이유만으로 외부 STL 경계로 남기지 않는다.
- [DataSystem.h](../../Engine/RenderEngine/DataSystem.h): 캐시 Entry, 폰트 소유자, retained asset 연결이 있다. 키 타입 변경 없이 조회 무할당, 약한/강한 소유 관계와 제거 순서를 보존한다.
- [LogStore.h:397](../../Engine/Utility_Framework/Diagnostics/LogStore.h): 발생 목록·그룹·해시 인덱스·유휴 목록이 연동된다. 그룹 제거가 문자열과 인덱스 소유를 함께 해제해야 한다.

문자열 키를 유지할 때는 기존 해시·동등성 또는 명시적 어댑터를 선택해야 한다. 정본의 기본 `ce::hash<T>`가 임의의 `std::string`, path, GUID 사용자 타입에 그대로 맞는다고 가정하지 않는다. 뷰 조회를 임시 소유 문자열 생성으로 바꿔 무할당 조건을 깨뜨리지 않는다.

### 정렬 계열은 현재 구현의 빈 범위

정본 모듈 export 목록 (`Engine/Containers/modules/Creator.Containers.ixx`, 로컬 기록)과 헤더를 확인했다. 현재 합격 정본에는 ordered map/set/multi 계열이 없다. 355건 모두 해시 컨테이너로 바꿀 수 있다는 결론은 내리지 않았다.

- [RHIPersistentHeapPolicy.h:137](../../Engine/RenderEngine/RHI/RHIPersistentHeapPolicy.h): offset 정렬 맵·size 중복 맵·allocated 맵이 함께 동작한다. 구현의 `lower_bound`, `equal_range`, 앞뒤 블록 병합이 실제 정렬 의미를 사용한다.
- [LXStyle.cpp:423](../../Lattice/ImGui/LXStyle.cpp): 정렬 맵 순회로 저장 내용을 쓴다. 해시 순회로 바꾸려면 출력 순서가 바뀌므로 직렬화 계약 검증이 필요하다.
- [DxCaptureFile.cpp:401](../../Engine/EngineDiagnostics/DxCaptureFile.cpp): 중복 허용 정렬 active 집합을 사용한다.
- [LXGraph.h](../../Lattice/Core/LXGraph.h): 정렬 맵이 외부에 노출된 내부 API·레이아웃·그룹 저장소를 구성한다. Lattice 소비자와 직렬화 경로를 같은 배치로 검토한다.

필요한 정렬 구현을 추가하는 경우 기존 합격 파일은 유지하되, 새 계열에는 별도 정확성·MSVC STL 성능 수용이 필요하다. 정렬이 필요 없는 사용처는 사용 계약을 확인한 뒤 해시 계열로 전환할 수 있다.

### deque·어댑터의 수명 차이

- [ProfileMarker.cpp:23](../../Engine/EngineDiagnostics/ProfileMarker.cpp): 소유한 이름의 `c_str()` 주소를 다른 표가 보관한다. 소스가 deque를 선택한 이유를 명시한다. 이동 가능한 문자열 배열로 치환하면 짧은 문자열의 주소가 깨질 수 있다. 문자열 타입 보류 중에도 이 컨테이너 수명 검토는 필요하다.
- [MiniaudioBackend.cpp:475](../../Engine/SceneRuntime/Audio/MiniaudioBackend.cpp): VoiceSlot 저장소는 네이티브 객체·콜백·슬롯의 주소와 중지/해제 순서를 검토해야 한다. 백엔드 파일이라는 이유로 외부 STL ABI로 분류하지 않는다.
- [EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp): 프레임 제출 큐는 GPU 완료·생산자/소비자 종료와 함께 검토한다. 고정 용량 링으로 바꾸려면 포화·역압 정책이 필요하다.
- [ReflectionUndo.h:100](../../Editor/EngineEntry/Editing/ReflectionUndo.h): unique_ptr 명령을 두 스택 사이로 이동한다. Undo 실패 시 원본 복구, Pop 시 명령 파괴, Play/Stop의 이력 분리를 보존한다.
- [EnhancedRenderGraph.cpp:2107](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp): 우선순위 큐의 작은 pass ID 우선 순서와 기존 render arena 할당자를 유지한다.

현재 `bounded_ring`은 고정 용량이며 일반 deque와 같은 성장 계약을 제공하지 않는다. 기존 락으로 보호하던 큐를 락 프리 큐로 바꾸는 것도 이번 분류의 자동 결론이 아니다.

### 실제 외부 STL 반환 경계

[ReflgenAuthoring.h:244](../../Engine/Utility_Framework/Reflection/ReflgenAuthoring.h)의 `read_bytes()`는 SDK `reflgen::reader` 가상 함수를 override한다. 설치된 SDK `reflgen/serial/reader.h:70`에서 `virtual std::vector<std::byte> read_bytes() = 0`을 직접 확인했다. 같은 함수의 결과 버퍼까지 2건이며 하나의 논리적 경계다. 내부 디코드 배열은 이미 ce 타입이다. 이 경계까지 없애려면 SDK 인터페이스 변경 또는 별도 SDK 경계 유지 정책이 필요하다.

## 실행 배치 권고

1. Effect 값 배열 59건과 비트셋 2건을 각각 소비자와 묶어 전환한다. 개별 회귀 후 배치 엔진 빌드·실행으로 검증한다.
2. 해시 맵 201건을 로컬 값 조회, 문자열·사용자 키 어댑터, 장기 소유 캐시의 세 배치로 나눈다. 헤더 공개 타입은 선언·구현·소비자를 함께 전환한다.
3. 해시 집합 92건은 API·노드 수명 차이를 해소한 뒤 묶는다. 집합 반환값을 STL처럼 사용하는 곳은 단순 치환하지 않는다.
4. 정렬 355건·deque 34건·어댑터 8건은 필요한 구현 범위와 사용처별 대체 가능성을 먼저 정한다. 정렬·중복·주소 안정성 요구를 포기해 잔여 수를 줄이지 않는다.
5. 외부 ABI 2건, 문자열·문자열 뷰, 함수 래퍼·비소유 뷰는 별도 목록으로 관리한다.

앞서 말한 5~10일은 이 분류 전의 추정이었다. 직접 대응 구현이 없는 계열과 새 성능 수용까지 모두 끝나는 확정 일정으로 사용하지 않는다. 이 분류는 우선순위·성능 병목의 실측이 아니다.

## 재현과 검증

기존 감사와 새 분류의 합계 9,396건이 일치하고, 753개 고유 위치와 분류 합계가 일치한다. 입력 소스 1,496개와 성능 합격 정본 62개 파일의 해시가 변하지 않았음을 확인했다. 엔진 코드를 변경하지 않았으므로 이번 분류를 위해 엔진 빌드·실행·성능 측정을 새로 수행하지 않았다.

- [분류 JSON](../../Build/Verification/ContainerMigration/NonStringBoundaries20261027verified/classification.json): 모든 위치·원문·템플릿 인수·분류·검토 표시.
- [분류 CSV](../../Build/Verification/ContainerMigration/NonStringBoundaries20261027verified/classification.csv): 파일/유형별 필터용.
- [검증 기록](../../Build/Verification/ContainerMigration/NonStringBoundaries20261027verified/verification.json): 합계·입력·정본 해시와 외부 SDK 근거.
- [재현 스크립트](../../Tools/build/classify-nonstring-container-boundaries.ps1): 새 출력 디렉토리를 지정해 재실행.

이전 구현 단계: [PSO 캐시·비동기 대기 맵 전환](PsoMapMigration20261027.html).

후속 구현: [배열·비트셋 61건 배치 전환](ArrayBitsetBatchMigration20261027.html). 이 분류표는 전환 전 시점이며 최신 잔여 저장 컨테이너 참조는 692건이다.
