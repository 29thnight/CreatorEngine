# 해시 맵 194건 배치 전환

문자열 타입과 성능 합격 정본을 유지하고, 남은 `std::unordered_map` 201개 타입 참조 중 **194건을 90개 파일에서 전환**했다. 별도 폰트 스냅샷 소비자 1개 파일도 함께 맞췄다. 정렬 컨테이너·집합·deque는 이번 배치에서 변경하지 않았다. 전체 저장 컨테이너 잔여량은 **692 → 498건**이다.

## 저장소와 키 정책

새 `Engine/Utility_Framework/Foundation/EngineNodeMap.h`는 엔진 사용처의 정책 연결 헤더다. `ce::engine_node_map`은 합격 정본의 `ce::node_hash_map`에 `ce::heap_allocator, false`를 적용한 데이터 멤버 없는 연결 타입이며 내부에 STL 맵을 보관하거나 우회하지 않는다. `Engine/Containers` 합격 구현 62개 파일은 수정하지 않았다.

- 기존 정수·포인터·GUID·사용자 키의 기본 표준 해시/동등성 정책과 호출자가 명시한 사용자 정책을 유지한다.
- 기존 `std::string`·`std::wstring` 키는 같은 문자열 타입으로 소유한다. 기본 정책에는 문자열 뷰 기반 해시·동등성을 제공해 뷰 조회를 임시 소유 문자열 생성으로 바꾸지 않는다. 표준 소유 문자열과 뷰의 해시가 같은지 실제 검사했다.
- 노드를 풀에 보관하지 않는다. erase/clear 시 값의 소멸자와 노드 해제가 실행되며, 재해시 후에도 원소의 주소가 유지된다. 비운 뒤 해시 인덱스 용량을 유지할 수 있고 총 메모리 크기·증가 수열이 MSVC와 동일하다는 주장은 하지 않는다.
- 이 헤더는 기존 엔진·SDK 키 정책의 연결 계층이다. 표준 해시·문자열 의존까지 모두 제거한 라이브러리라는 주장은 하지 않는다. 저장소 구현의 정본은 그대로다.
- `at()`의 실패는 기존처럼 `std::out_of_range`로 전달한다. 정본의 내부 bounds 예외가 엔진의 기존 예외 처리 경계를 빠져나가지 않도록 연결 타입에서 처리하고 const/non-const 경로를 검사했다. 예외 생성은 실패한 checked access에만 해당한다.

주요 범위는 Scene/Animation/Audio의 상태·캐시, DataSystem, DX12/Vulkan RHI 맵, 렌더 그래프 인덱스, FBX/glTF 로컬 조회, Lattice 그래프, Editor 상태·명령·ImGui 캐시와 Player 명령이다. 기존 문자열 타입, 강한/약한 소유자·unique_ptr 값, 잠금·GPU 완료 대기·은퇴 큐·세대 정책은 변경하지 않았다. unordered 순회 순서는 보장하지 않으며 정렬 의미가 있는 map/set은 그대로 남겼다.

## 별도 계약으로 남긴 7건

| 파일 | 참조 | 보존한 이유 |
|---|---:|---|
| TextureFramePins.h | 2 | `extract`한 노드가 잠금 범위 밖에서 최종 이미지 소유자를 파괴한다. 이 순서를 일반 erase로 바꾸지 않음 |
| ScenePhysicsSimulation.h/cpp | 5 | 기존 노드를 추출·재키잉·재삽입해 물리 교체를 롤백하며, 예약 후 추가 할당 없이 게시하는 경로가 있음 |

이들은 전환 완료가 아니라 별도 구현·검증 대상으로 남아 있다. 외부 API 때문에 면제한 것이 아니다. 표준 노드 추출 API가 없는 정본에 맞추려고 소유권 또는 무할당 롤백 계약을 약화하지 않았다.

## 소비자 연결 수정

단순 타입·include 치환 외에는 다음 연결만 조정했다.

1. AppBackendWinAPI named module은 `module;`의 위치를 유지하고 `Creator.Containers`를 import한다. 이 한 사용처는 기존 표준 키 정책을 명시한 직접 `ce::node_hash_map` 타입이다.
2. Scene의 물리 컴포넌트 순회 4곳은 `views::values` 대신 구조화 바인딩으로 `.second`를 읽는다. 포인터 값·호출 순서와 처리는 그대로다.
3. DataSystem 폰트 스냅샷과 Editor 블랙보드 정렬 스냅샷은 새 맵의 `ce::key_value`를 기존 `std::pair` 결과에 키·값으로 명시적으로 복사한다. 폰트 반환 타입과 공동 소유를 유지하고 필요한 배열 공간을 예약한다. 이 소유 스냅샷 생성은 할당이 허용되는 경로다.
4. EnhancedSceneRendererSelfTest의 중첩 초기값은 RHI 정보 타입을 명시한다. 테스트 값 123/124와 리소스 설명은 그대로다.

최초 빌드에서 발견한 모듈 위치·tuple view·pair 변환·초기값 추론 오류와 각 수정 이후의 로그를 모두 `Build/container-hash-map-editor*.log`로 보존했다. 검사를 통과시키기 위해 정본 또는 테스트 기대값을 변경하지 않았다.

## 네이티브 정책·소유권 검사

`HashMapMigrationProbe`는 실제 모듈 소비자이며 Debug **516,167건**, Release **516,164건**을 통과했다.

- 충돌 해시를 사용하는 8,192개 삽입/갱신/삭제 연산과 모든 생존 값의 결과를 MSVC STL 모델과 비교했다.
- 긴 문자열 hit/miss와 넓은 문자열 조회를 8,192회 반복했다. Debug CRT 후크에서 호출 스레드 할당 0회였다. 측정 구간에는 종료 문자가 없는 부분 문자열 뷰가 포함된다.
- 키를 넣은 뒤 호출자 버퍼를 바꿔도 키가 유지되며 내부 NUL과 뷰 길이가 키 정체성에 포함된다. 문자열·문자열 뷰 해시 일치를 확인했다.
- reserve/rehash 후 키·값 노드 주소가 유지됐고 copy/move 값과 명시적 pair 스냅샷이 일치했다.
- 공동 소유자가 남아 있으면 erase 후에도 객체가 유효하고, 마지막 소유자를 놓으면 파괴됐다. unique_ptr 값은 clear에서 즉시 파괴됐다.
- Debug에서 원소 1개 erase 시 노드 해제 1회, 32개 clear 시 노드 해제 32회를 관측했다. 인덱스 용량은 유지됐다.
- 리플렉션의 KeyedRange·FillableKeyed 형상 계약을 컴파일 시 확인했다. 이 형상 검사만으로 모든 직렬화 값 왕복을 검증했다고 주장하지 않는다.

최종 결과는 `Build/Verification/ContainerMigration/HashMapBatch20261027/native-accepted/result.json`, 재현 스크립트는 `Tools/regression/verify-hash-map-migration.ps1`이다. 이 검사는 정확성·할당·수명 검사이며 속도 벤치마크가 아니다. 기존 호출자가 명시적으로 만드는 임시 문자열이나 소유 스냅샷의 할당까지 모두 없앴다는 뜻은 아니다.

## 실제 엔진과 DX12 검증

최종 소스로 Effect 패키지를 포함한 Debug Editor를 빌드하고 **17명령 제품 실행 검증**을 통과했다. 실제 새 GPU 프레임, 렌더 프록시 적용, 두 번의 8,192건 컨테이너 전송, Play/Stop·장면 복원과 정상 종료를 확인했다.

실제 DX12 장치의 MaterialRenderBindingsProbe도 Debug **8,786건**, Release **8,781건**을 통과했다. 두 구성 모두 GPUValidation을 켜고 GPU 64성분·4프레임을 검증했다. 변경한 텍스처·메시·persistent heap 맵을 포함하는 엔진 코드를 빌드하고 기존 재사용·소유 프레임·실패 롤백·늦은 반환·노드 해제 회귀를 유지했다. 이 결과를 모든 Audio/Vulkan 실행 경로의 런타임 수용으로 확대하지 않는다.

제품 증거는 `Build/container-hash-map-editor-final.log`, `Build/Verification/ContainerMigration/HashMapBatch20261027/editor-final/acceptance.json`이다. DX12 증거는 같은 배치의 `material-final/result.json`과 구성별 빌드·실행 로그다.

함께 변경한 Player 명령 맵은 Debug Player 빌드로 확인했다. 증거는 `Build/container-hash-map-player.log`와 최종 Player 실행 파일·runtime DLL 해시다. 제품 실행 수용은 위 Editor 실행에 해당한다.

## 정본·작업 보존과 잔여량

합격 정본 62개 파일의 해시가 일치하며 모듈 헤더 26개·소스 도구 어댑터 31개 동기화 검사가 통과했다. 이전 시점의 감사 입력 중 이번 변경 허용 목록 밖 소스 **1,405개**도 해시가 유지됐다. 대상 파일의 수정 전 사본·해시는 배치의 `before`, `before.json`, `consumer-before.json`에 보존했다. 미추적 Effect 작업을 비롯한 다른 소스를 초기화하거나 스테이징하지 않았다.

HEAD 기준 공백 검사에는 기존 BTBuildGraph/SceneManager 수정의 공백 문제 2건이 남아 있다. 작업 전 사본을 기준으로 기존 파일 91개와 신규 파일 5개를 검사해 이번 배치가 추가한 공백 문제는 없음을 확인했다. 기존 작업의 해당 줄은 수정하지 않았으며 `whitespace-verification.json`에 구분해 기록했다.

기존 성능 `20261027-tailhint-full1`은 원시 258개 조건/프로세스 결과(각 15표본)와 입력·바이너리 해시를 재검증해 일반 82/82, MPSC 단일·주 기준 각 4/4, FAA/PPL 6/6, 기타 큐 8/8 PASS를 유지했다. 기록은 외부 `CreatorContainers/results/20261027-hash-map-baseline-reverify.log`다. 새 연결 정책의 사용처별 성능이나 프레임 속도를 새로 측정한 결과는 아니다.

| 잔여 저장 컨테이너 | 타입 참조 |
|---|---:|
| 정렬 map/set/multi 계열 | 355 |
| 해시 집합 | 92 |
| deque | 34 |
| stack/queue/priority_queue | 8 |
| 별도 노드 추출 맵 | 7 |
| reflgen SDK vector 경계 | 2 |
| 합계 | **498** |

전체 감사는 **9,335 → 9,146건**이다. 맵 참조는 194건 줄었고, 정책 선언 및 명시적 폰트 결과 배열 선언에 문자열 타입 참조 5건이 추가돼 순감소는 189건이다. 기존 문자열 타입을 다른 타입으로 전환하지 않았다. 문자열 참조 8,384건과 함수 래퍼 264건은 별도 관리한다. 후보 파일은 961개다. 수치는 타입 참조 수이며 독립 객체나 작업 개수가 아니다.

최신 분류 JSON/CSV는 `Build/Verification/ContainerMigration/HashMapBatch20261027/remaining-accepted`에 있고, 최종 입력과 결과는 같은 배치의 manifest에 묶었다. 이전 단계: [배열·비트셋 61건 배치](ArrayBitsetBatchMigration20261027.html).
