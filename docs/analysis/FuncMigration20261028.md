# func 모듈 성능 수용과 엔진 전환

2026년 10월 28일, func 모듈의 성능 15개 항목을 동일 바이너리로 3회 측정해 45개 판정을 모두 통과했다. 힙 대상 이동은 MSVC STL 대비 1.01~1.02배로 기존 1.10배 기준을 충족했다. 엔진 std::function 참조 264건을 98개 파일에서 교체했고 실제 Editor Debug 빌드와 실행 검증을 통과했다.

## 힙 이동 원인과 최적화

호출 대상과 관리 정보를 따로 보관하던 구현은 힙 대상 이동에서도 두 상태를 갱신했다. 중복 주소 저장을 제거한 비교에서 이동 비율은 약 1.28배에서 1.16배로 줄었다. 대상과 관리 정보를 같은 모델에 배치해 힙 이동을 단일 포인터 전달로 바꾼 비교에서는 1.06배로 개선됐다.

이동 대입은 새 대상과 원본의 빈 상태를 확정한 뒤 기존 대상을 원래 allocator로 해제한다. 인라인 대상에는 별도 경로를 사용한다. noexcept 호출의 함수 포인터 캐시와 고정 용량 호출의 작은 스칼라 값 전달로, 관리 정보 재조회와 인자 전달 비용도 줄였다. 원인 판단은 통제 부하의 구현 비교와 생성 코드 확인에 근거한다. 중간 버전과 실패한 표본도 삭제하지 않았다.

## 기존 기준의 전체 수용

MSVC 19.51 C++23 Release named-module 바이너리를 사용했다. 각 조건은 1초 예열 후 양쪽 각각 15개 100ms 표본을 수집했다. CPU 4에 실행 스레드를 고정하고 CPU 5에는 기존 pause 부하 도우미를 적용했다. 계측 중 빌드와 소스 감사를 실행하지 않았으며 두 번째 전체 실행은 STL을 먼저 측정했다.

판정 기준은 CE/STL 중앙값 ≤ 1.10, 양쪽 각각 정렬 표본 [13]/[1] ≤ 1.30이다. 표는 CE/STL 비율이며 낮을수록 빠르다. unique 계열은 std::move_only_function, 그 외는 std::function과 비교했다. 실패 표본 제거, 조건별 결과 이어 붙이기 또는 기준 변경은 사용하지 않았다.

| 항목 | 1회 | 2회 역순 | 3회 |
| --- | ---: | ---: | ---: |
| aligned_construct | 1.066 | 1.065 | 1.066 |
| inline_owned_copy | 0.592 | 0.592 | 0.592 |
| inline_unique_move | 0.889 | 0.881 | 0.885 |
| noexcept_unique_call | 0.904 | 0.851 | 0.899 |
| owned_heap_call | 0.999 | 0.998 | 1.002 |
| owned_heap_construct | 1.038 | 1.054 | 1.042 |
| owned_heap_copy | 1.032 | 1.015 | 1.031 |
| owned_heap_move | 1.017 | 1.016 | 1.015 |
| owned_ptr_call | 0.998 | 1.004 | 0.996 |
| owned_sbo_call | 0.996 | 1.004 | 1.001 |
| owned_sbo_construct | 0.831 | 0.824 | 0.834 |
| owned_sbo_copy | 0.712 | 0.720 | 0.712 |
| owned_sbo_move | 0.718 | 0.714 | 0.717 |
| ref_call | 0.566 | 0.568 | 0.563 |
| unique_capture_call | 0.841 | 0.850 | 0.840 |

[45개 최종 판정](../../Build/Verification/ContainerMigration/FuncMigration20261028/performance-verdict.json), [고정 소스 해시](../../Build/Verification/ContainerMigration/FuncMigration20261028/performance-frozen-inputs.json), [원시 표본](../../Build/Verification/ContainerMigration/FuncMigration20261028/acceptance1.csv)을 보존했다. 기존 82개 항목과 PPL 수용 자료는 유지했으며 이번에는 기존 구현 78개 파일의 해시를 확인하고 새 func 항목을 측정했다.

## 구현과 소유권 계약

정본은 Engine/Containers/func/Creator.Containers.Func.ixx이며 실제 엔진은 Creator.Containers.Func를 import한다. func.h는 sync-func.ps1으로 생성하는 standalone 도구용 파생 파일이다. 외부 CreatorContainers/func/FuncValidation.proj도 엔진 내부 프로젝트를 참조하며 구현 복사본을 두지 않는다.

| API | 계약 |
| --- | --- |
| ce::owned_func | 소유하며 복사 시 대상을 독립 복제 |
| ce::unique_func | 대상을 소유하며 이동만 허용 |
| ce::func_ref | lvalue 대상의 비소유 참조, 대상 수명 연장 없음 |
| ce::inline_owned_func | 고정 인라인 저장과 독립 복사 |
| ce::inline_unique_func | 고정 인라인 저장과 이동 소유 |

기본 일반 소유형은 이 환경에서 64바이트, func_ref는 16바이트다. 기본 인라인 대상 용량은 48바이트이며 크기·정렬을 넘거나 이동이 던질 수 있는 대상은 힙에 저장한다. 고정 용량 소유형은 용량·정렬·nothrow 이동 조건을 컴파일 시 검사한다. 캡처 자체의 내부 할당은 래퍼 무할당 보장에 포함하지 않는다.

조회·호출·힙 대상 이동은 래퍼 allocator를 사용하지 않으며 reset과 소멸 시 대상을 해제한다. std::function 또는 std::move_only_function을 내부 저장소로 사용하지 않는다. 기존 모듈과 같은 표준 타입 특성·호출 보조·예외 지원은 사용한다.

## 사용처 전환과 보존

복사되는 레지스트리·Delegate·Progress·편집기 콜백은 owned_func로 유지했다. job_group, 완료 observer, continuation, indexed·graph 작업과 ClusterStreaming 작업 큐는 unique_func로 전달한다. parallel_for는 청크별 복사가 필요하므로 owned_func를 유지했다.

assist_until의 세 선언·정의만 func_ref로 바꾸고 호출부에 이름 있는 lvalue predicate를 두었다. 참조 대상은 동기 대기가 끝날 때까지 호출 스택에 살아 있으며 저장 큐로 보내지 않는다. 보관·복사될 수 있는 const-reference 콜백을 일괄 비소유형으로 바꾸지 않았다.

미추적 파일을 포함한 Engine·Editor·Lattice·Player·HeadlessHost 감사에서 callable 잔여는 0건이다. 문자열 참조 8,388건은 그대로 보류했고, 저장 컨테이너 잔여 2건은 SDK 경계의 std::vector<std::byte>다. 기존 합격 구현 78개 파일의 해시를 유지했다. 이번 소비자·진단 범위 밖 원본 1,401개 파일과 그중 미추적 20개 파일도 해시가 같다.

[전환 내역](../../Build/Verification/ContainerMigration/FuncMigration20261028/migration-receipt.json), [원본 보존 확인](../../Build/Verification/ContainerMigration/FuncMigration20261028/source-preservation.json), [잔여 분류](../../Build/Verification/ContainerMigration/FuncMigration20261028/remaining-final/classification.json)를 보존했다.

## 실제 검증

독립 실행, named-module Debug·Release, AddressSanitizer에서 각각 216,464건을 통과했다. 독립 복사, move-only 캡처, 참조 반환, 임시 대상 거절, 과도한 정렬, allocator 전달, 실패한 복사·할당의 롤백, 즉시 해제, 빈 상태 이동과 noexcept 캐시를 포함한다. 동일 대상을 사용한 STL 대조 무작위 연산 1만 회로 빈 상태·타입·호출 결과를 비교했다. 기존 대상의 소멸자가 새 대상과 비워진 원본을 관찰하는 검증도 포함한다.

Delegate·Jobs 모듈과 standalone 검증은 Debug·Release에서 통과했다. Jobs는 각 10,770건이며 이동 전용 캡처의 일반·indexed·graph 작업과 완료 observer가 handle 준비 전에 자원을 해제하는지 확인했다.

실제 Editor Debug 빌드 후 17개 명령을 실행했다. 두 번의 컨테이너 진단으로 총 16,384회 전송과 func 소유권·무할당 조회·즉시 해제를 확인했다. DX12 렌더러 준비, Play/Stop, 편집 장면 객체 수와 변환 복원, 정상 종료를 확인했다. 실제 worker 제품 검증도 bundle 32개 제출·32개 완료, external read 64회, inline read 0회, foliage 73개 완료로 통과했다.

[엔진 실행 수용](../../Build/Verification/ContainerMigration/FuncMigration20261028/editor-final/acceptance.json), [실제 명령 결과](../../Build/Verification/ContainerMigration/FuncMigration20261028/editor-final/results.jsonl), [작업 실행 결과](../../Build/Verification/ContainerMigration/FuncMigration20261028/worker-product/results.jsonl), [ASan 로그](../../Build/Verification/ContainerMigration/FuncMigration20261028/asan-final.log)를 확인할 수 있다.

완료 대기를 제거한 고의 결함은 Debug·Release에서 모두 검출했다. [일반 실행과 고의 결함 검증](../../Build/Verification/ContainerMigration/FuncMigration20261028/worker-pool-mutations3.log), [전체 수용 기록](../../Build/Verification/ContainerMigration/FuncMigration20261028/acceptance.json)에 최종 판정과 소스·바이너리·로그 해시를 보존했다.
