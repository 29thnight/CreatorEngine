# 컨테이너 엔진 통합과 실행 검증

이 문서는 초기 통합 당시의 검증 기록이다. 이후 전체 교체 진행과 최신 검증 경계는 [전체 컨테이너 전환 진행 기록](FullContainerMigration20261026.md)을 따른다.

2026년 10월 26일 기준으로 컨테이너 구현 정본을 `Engine/Containers`로 통합했다. 엔진은 이 내부 소스만 빌드하며, 외부 CreatorContainers 솔루션은 같은 소스를 대상으로 테스트와 측정을 수행한다. STL/PPL 교체는 렌더 명령 큐, 에셋 가져오기, 지오메트리 캐시, 렌더러 공통 배열에 적용했다. 저장소 전체의 STL 제거는 완료되지 않았다.

## 구현 소유권과 빌드 경로

`Engine/Containers/modules`의 22개 인터페이스 파티션과 `Creator.Containers.ixx`를 EngineContracts 프로젝트에 직접 등록했다. `Creator.Concurrency`는 이 모듈을 재수출한다. `ThirdParty/CreatorContainers`와 외부 저장소 경로에 대한 엔진 빌드 의존성은 없다.

호환 헤더 `Engine/Containers/include/ce`는 모듈 export 본문에서 생성한다. reflection 파서와 standalone 검사에만 사용하며 한 바이너리 안에서 named module API와 섞지 않는다. `sync-headers.ps1 -Check`와 도구 어댑터 31개 일치 검사가 통과했다.

외부 `../CreatorContainers`의 CMake는 `CE_IMPLEMENTATION_ROOT`를 통해 엔진 내부 소스를 직접 참조한다. 기본값은 인접 CreatorEngine의 `Engine/Containers`다. PowerShell 드라이버도 같은 루트를 사용하며 CMake 캐시에 전달한다. 활성 외부 `modules`와 `include`에는 구현 `.ixx`와 `.h`가 0개다. 이전 복사본은 외부 `archive/engine-canonical-20261026`에 보존했고 빌드 입력에서 제외했다. 과거 성능 원시 자료와 당시 바이너리는 유지한다.

외부 참조 증거는 `../CreatorContainers/results/20261026-engine-canonical-reference.json`에 루트, 소스 해시, 활성 복사본 수, 테스트 로그로 기록했다. 성능 측정 스크립트는 정본으로 벤치마크를 다시 빌드한 후 해시를 동결한다.

## 교체한 엔진 사용처

| 사용처 | 변경 | 보존한 동작 |
| --- | --- | --- |
| ProxyCommandQueue | PPL 큐를 `ce::lock_free_queue`, 배치와 팔레트 저장소를 `ce::dynamic_array`로 교체 | epoch 보류 순서, 소유권 전달, 종료 시 drain, 주기적 reclaim |
| EditorAssetPresentation | PPL 가져오기 큐, 대기 배열, 확장자 맵을 자체 큐·배열·문자열 해시 맵으로 교체 | 경로 소유권, 확장자 정규화, 대기 항목 확인과 종료 정리 |
| CollisionGeometryLibrary | STL flat map을 자체 `basic_flat_hash_map`으로 교체 | UUID 전체와 revision 키 비교, 준비된 복사본의 swap 발행, 실패 시 기존 캐시 보존 |
| `ce::render_vector` | `std::vector` 별칭을 `dynamic_array<T, render_byte_allocator>`로 교체 | 기존 mimalloc 할당·해제, 렌더 그래프·리소스 테이블·배치·arena의 소유권 |

렌더러 배열은 8개 소스 파일의 공통 별칭 사용처 29곳에 적용된다. 새 바이트 할당기는 기존 `ce::heap`을 호출하며 실패 시 `bad_alloc`을 전달한다. 자체 배열에 개수 생성자, 범위 대입, iterator/type 별칭, `front`를 추가했다. 범위 대입은 별도 버퍼를 완성한 후 swap하므로 자기 범위를 입력으로 받을 수 있고, 복사 예외 시 기존 목적 배열을 보존한다. 빈 범위에서는 null 포인터 뺄셈을 수행하지 않는다.

기존 mimalloc STL allocator는 `ce::stl_heap_allocator<T>`로 이름을 구분했다. SHA-256 네임스페이스는 `ce::crypto`로 옮겨 자체 컨테이너의 `ce::hash<T>`와 충돌을 해소했다. 해시 알고리즘과 저장 포맷은 변경하지 않았다.

락 프리 큐의 알고리즘과 원자 연산은 이번 저장소 통합 및 렌더러 배열 전환에서 변경하지 않았다. 동적 큐의 일반 메모리 할당까지 무대기 또는 락 프리라고 확대해서 주장하지 않는다.

## 검증 환경과 성능 결과의 범위

CreatorEngine master를 `27394992c`로 fast-forward 동기화했다. 동기화 전 미커밋 파일과 패치는 인접 `CreatorEngine-container-backup-20261026-140730`에 보존했고 겹친 렌더러 파일은 3-way 병합했다. 커밋과 푸시는 수행하지 않았다.

엔진의 결합된 reflection 모듈 그래프는 설치된 MSVC 19.51, VCToolsVersion 14.51.36231로 검증한다. 19.50에서는 C1054/C1001이 재현됐다. `ContainerToolchain.props`가 설치된 검증 버전을 선택하고 이전 버전의 EngineContracts 빌드를 명확히 거부한다. 19.51에서 export된 명시적 변수 템플릿 특수화가 실패하는 문제는 `is_void<T>`가 기존 `same_type<T, void>` 판정을 사용하도록 바꿔 해결했다.

기존 독립 성능 수용은 MSVC 19.50 모듈 바이너리의 [모듈 최적화 계측 결과](ModuleOptimizationDiagnostics20261026.md)를 기준으로 한다. 일반 82개 항목 3회, MPSC, FAA, SPSC/MPMC 기준을 통과했다. 이번 엔진 기능 검사와 sanitizer 결과는 새 컴파일러에서의 전체 성능 수용을 대신하지 않는다. 렌더러 프레임 성능 향상도 이 통합 검사만으로 주장하지 않는다.

## 최종 소스 검증 결과

| 검사 | 결과 | 증거 |
| --- | --- | --- |
| 외부 MSVC 모듈 기능 검사 | PASS 9/9 | 외부 results/engine-canonical-final2-driver.log |
| 외부 MSVC AddressSanitizer | PASS 9/9 | 외부 results/engine-canonical-asan2-driver.log |
| Linux Clang 18 ThreadSanitizer 모듈 검사 | PASS 7/7 | 외부 results/engine-canonical-tsan-driver.log |
| 최종 Debug Editor 빌드와 실행 | PASS | Build/container-final-editor-Debug.log, Build/Verification/ContainerIntegration/FinalDebug/acceptance.json |
| 최종 Release Editor 빌드와 실행 | PASS | Build/container-final-editor-Release.log, Build/Verification/ContainerIntegration/FinalRelease/acceptance.json |
| 최종 Debug Player 빌드 | PASS | Build/container-final-player-Debug2.log |
| 최종 Release Player 빌드 | PASS | Build/container-final-player-Release.log |

Editor 실행 검사는 실제 `CreatorEditor.exe`를 격리된 workspace로 시작한다. offscreen 모드에서도 제품 렌더러의 새 GPU 프레임 완료를 요구한다. 17개 commandlet 명령으로 4개 생산자의 8192개 항목을 두 차례 전달하고 소유권과 중복·누락을 검사한다. 지오메트리 캐시 조회와 발행 실패 롤백, 실제 light proxy queued/applied, Play/Stop 상태 전환과 장면 transform 복원, 프로세스 정상 종료를 확인한다. 실행 증거에는 exe/runtime DLL과 결과 파일 해시를 함께 남긴다. Debug와 Release 각각 17개 명령이 성공했고 종료 코드는 0이다. 두 실행 모두 검증한 원래 장면의 3개 오브젝트 transform을 복원했다. 기존 사용자 workspace 파일은 동기화 전 백업과 SHA-256이 같다.

기존 독립 cooked geometry 검사기는 현재 브랜치의 링크 의존성 오류로 통과하지 못했다. 이번 캐시 교체의 조회·트랜잭션 회귀는 실제 Editor 런타임 probe로 검증한다. cooked geometry의 pak 경유 전체 회귀 통과를 뜻하지 않는다. Player는 빌드 검증 범위이며 패키징된 Player 실행 수용은 별도다.

추적 중인 Engine/Editor 소스의 PPL 직접 소비 패턴은 0건이다. 검사 패턴은 `concurrency::concurrent_*`와 PPL/concurrent 컨테이너 include이며, 증거는 `Build/Verification/ContainerIntegration/remaining-ppl-uses.json`이다.

## 남은 STL 전환 범위

추적 중인 Engine/Editor 소스에서 주요 STL 컨테이너 템플릿 사용 패턴을 검색한 결과 701개 파일의 4309개 행이 남았다. 이는 컨테이너 인스턴스 개수가 아니며 함수 인수·반복 사용과 외부 API 경계도 포함한다. `std::string`의 비템플릿 표기 등 검색 밖 사용처도 있어 전수 개수로 해석하면 안 된다. 목록은 `Build/Verification/ContainerIntegration/remaining-stl-storage.json`에 보존했다.

충돌 지오메트리의 공개 vector/span 포맷, reflection/serialization 인수, 에셋·장면 저장소와 도구 UI는 추가 전환 대상이다. 외부 라이브러리와 직렬화 경계를 먼저 분리한 뒤 저장소를 바꾸고 소비자 회귀를 실행해야 한다. 이번 빌드·실행 PASS를 저장소 전체 STL 제거 완료로 해석하지 않는다.
