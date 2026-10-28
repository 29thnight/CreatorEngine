# 자체 값 타입과 컨테이너 구현 정리 — 2026-10-27

**pair·tuple·get·tie·apply를 자체 모듈로 구현하고, 엔진 111개 파일의 참조 337건을 전환했다. 기존 구현 52개 파일의 서식을 정리했으며, 최종 Editor 빌드·실행과 Debug/Release DX12 검증이 통과했다.** 문자열 타입은 유지했다.

새 값 타입 성능은 7개 항목을 세 프로세스에서 각각 15표본씩 측정해 모두 통과했다. 다만 별도로 실행한 기존 컨테이너 전체 재측정은 **일반 79/82**다. 대형 자료 3건의 편차가 기준을 넘어 이번 전체 캠페인을 PASS로 판정하지 않는다. 이전 합격 원시와 정리 전 구현은 보존했다.

## 구현과 모듈 경계

정본은 `Engine/Containers/values/Creator.Containers.Values.ixx`다. `Creator.Containers.Values`를 EngineContracts와 독립 static-CRT RuntimeLauncher 양쪽에서 각자의 CRT에 맞춰 컴파일한다. 엔진 빌드는 외부 테스트 디렉토리를 참조하지 않는다.

자체 pair 필드와 tuple의 인덱스별 저장소를 구현했다. 표준 pair·tuple을 내부 저장소로 사용하지 않으며, apply는 자체 인자 전개와 호출 구현을 사용한다. get의 인덱스·고유 타입 접근, 네 가지 cv/ref 형태, tie, make_pair, make_tuple, forward_as_tuple, 구조적 바인딩을 지원한다. 요소 생성·소멸 순서는 현재 MSVC STL과 비교했다.

표준 타입 특성·비교 범주·참조 래퍼와 tuple 프로토콜은 사용한다. 외부 std::tuple·pair·variant 접근에는 상호 운용 경계를 둔다. **표준 라이브러리 전체 의존성을 제거했거나 표준 tuple API 전체를 제공한다는 의미는 아니다.** 허용된 tuple_size·tuple_element 특수화만 추가하며 표준 함수에 임의의 오버로드를 넣지 않았다.

참조 제품은 빌리며 대상의 수명을 늘리지 않는다. 변환 임시 객체에 연결되는 참조, 임시 소유 제품에서 빌린 참조로의 변환을 제한한다. 외부 get·멤버 호출의 참조 반환과 예외 전파도 검사했다. 제품 자체는 동적 할당을 하지 않으며 요소 타입의 할당과 해제는 해당 타입이 수행한다. 임의의 scoped allocator 전파는 구현 범위에 포함하지 않는다.

헤더 `values/value_types.h`는 `values/sync-values.ps1`로 생성한다. 외부 `CreatorContainers/value-types/ValueTypes.proj`는 엔진 내부 프로젝트를 빌드하는 검사 진입점이다. 외부 .slnx의 SDK 경로 전파 실패 기록은 보존했고, 최종 검사에는 실제 통과한 MSBuild 진입점을 사용했다.

## 사람이 읽는 코드 정리

기존 C++ 파일 **55개를 비교했고 52개가 실제 변경**됐다. 함수·형식 정의 사이의 빈 줄, 들여쓰기, 긴 선언과 조건식을 정리했다. clang-format 22.1.3에서 중괄호 자동 삽입·include 정렬·using 정렬·한정자 이동을 끄고, 선언 순서와 플랫폼 분기를 유지했다. 상세 설정은 배치의 `format-config.yaml`에 보존했다.

문자열 리터럴을 보존하고 공백·주석·전처리 줄 이어쓰기를 정규화한 비교가 55/55 일치했다. 해당 구현에는 __LINE__·__COUNTER__·__FILE__·#line 사용이 없음을 확인했다. 이는 코드 정리의 동등성 증거이며 새로운 전체 성능 PASS를 대신하지 않는다.

Allman namespace를 읽도록 기존 헤더 생성기의 메모리 설정 경계 탐색을 보완했다. 새 모듈의 불필요한 C 헤더/전역 모듈 조각과 중복 연결 include를 정리했다. 기존 공개 API와 플랫폼별 intrinsic 분기는 유지했다. 기존 모듈/헤더 26개, 값 타입 헤더와 소스 도구 어댑터 31개의 동기화 검사가 통과했다.

## MSVC STL 대비 값 타입 측정

Windows 11 Pro, i7-8700K 6코어/12스레드 환경에서 **MSVC 195136260 / STL 145, C++23 모듈 소비 바이너리**를 측정했다. 16,384개 레코드, 구현별 100ms 이상 배치, 각 1초 워밍업, CPU4·측정 스레드 above-normal·CPU5의 자체 SMT 보조 프로세스를 사용했다. 프로세스 3개, 각 15표본을 전부 유지했고 두 번째 프로세스는 조건과 A/B 순서를 뒤집었다. 할당과 출력은 측정 밖에 두고 체크섬을 대조했다. CPU 시간과 스레드 사이클도 기록했다.

기준은 각 조건·프로세스의 자체/STL 중앙값 비율 ≤1.10, 양쪽 정렬 15표본 x[13]/x[1] ≤1.30이다. 최종 **21/21 조건·프로세스가 통과**했다. 최악의 중앙값 비율은 **1.0311**, 양쪽 최대 편차는 **1.2285 미만**이다.

| 항목 | 범위 |
|---|---|
| pair 생성·읽기 | 두 u64 필드 쓰기와 읽기 |
| tuple 생성·읽기 | 네 u64 필드 쓰기와 읽기 |
| tuple apply | 자체/표준 함수 전개 |
| tuple 비교 | 사전식 비교 |
| tie 비교 | 세 필드의 빌린 참조 제품 |
| 멤버 apply | 멤버 함수 호출 |
| tuple 정렬 | 같은 std::sort에서 값 타입만 교체 |

초기 비교 구현은 tuple 비교 약 1.92~2.41배, 정렬 약 1.45배로 느렸다. 중간 비교 범주를 갱신하던 경로에서 비교 결과를 직접 반환하는 less 경로로 바꾼 진단 측정에서 개선됐다. 50ms 진단 값과 최종 100ms 수용 값은 구분한다. 이후 normal 우선순위 캠페인의 tuple 비교 편차 1.31 실패도 남겨 두었으며, 기존 측정 프로토콜과 우선순위를 맞춘 최종 캠페인을 별도로 판정했다. 실패 표본을 최종 표본으로 바꾸거나 한도를 완화하지 않았다.

최종 원시는 `Build/Verification/ContainerMigration/ValueTypes20261027/values-controlled-{1,2,3}.csv`, 판정은 `values-controlled-summary.json`이다. 측정 입력 해시는 `value-measurement-inputs.json`에 있으며 종료 후 모두 일치했다. 이 측정은 해당 값 타입 작업의 비교이며 모든 요소 타입·OS·실제 프레임 시간의 동등성을 입증하지 않는다.

## 기존 컨테이너 전체 재측정의 미달

기존 82개는 기존 독립 모듈 바이너리 조건을 유지한 MSVC 195035730 / STL 145 캠페인 `20261027-values-format-full1`로 별도 측정했다. 일반 246개와 MPSC 원시를 포함한 258개 조건·프로세스 결과의 15표본, 입력·바이너리 해시를 재검증했다.

| 범위 | 결과 |
|---|---:|
| 일반 컨테이너 | **79/82, FAIL** |
| MPSC 단일 전송 | 4/4 |
| MPSC 주 기준 | 4/4 |
| FAA/PPL 확장 큐 | 6/6 |
| SPSC/기타 큐 | 8/8 |

| 일반 미달 조건 | 세 프로세스 최대 자체/STL 비율 | 자체 최대 편차 | STL 최대 편차 |
|---|---:|---:|---:|
| map_hit / 65,536 | 0.5587 | 1.0298 | **2.6496** |
| map_miss / 65,536 | 0.3262 | 1.1332 | **1.4708** |
| string_find / 65,536 | 0.1865 | **1.3534** | 1.0381 |

중앙값 비율은 통과했고 편차 기준에서 실패했다. 같은 map_hit 프로세스의 STL 표본 8은 벽시계 645.6093ms·스레드 CPU 453.125ms, 표본 10은 193.4126ms·187.5ms였다. 사이클도 약 14.72억 대 7.13억으로 달랐다. 대기 구간과 실행 중 사이클 증가가 함께 관찰됐지만 특정 외부 프로세스·페이지 폴트·캐시 경합을 단독 원인으로 확정할 정보는 부족하다.

이전 `20261027-tailhint-full1`의 PASS는 정리 전 소스 manifest와 함께 남겼다. 새 소스 해시와 이번 FAIL 판정은 분리해 기록한다. 락 프리 큐 알고리즘과 메모리 순서는 변경하지 않았다. 이번 결과를 전체 재수용 완료로 표시하지 않는다.

## 전환과 실제 엔진 검증

Engine·Editor·Lattice 111개 파일에서 참조 337건을 제거/전환했다. pair 179건(직접 emplace로 바꾼 SpriteSheet 1건 포함), tuple 29건, get 83건, tie 23건, apply 20건, make_pair 3건이다. 표준 map allocator의 정확한 value_type 요구 때문에 `arena_map`의 std::pair 1건은 유지했다. 표준 프로토콜 특성과 SDK 내부 제품을 무리하게 바꾸지 않았다.

원본과 대조해 값 타입 이름·연결부 및 명시한 두 map 경계 외 변경이 없음을 111개 파일에서 확인했다. SpriteSheet의 CP949를 수정 중 잘못 읽은 문제는 원본 대조에서 발견해 원래 인코딩과 내용으로 복원한 뒤 최종 빌드·실행을 다시 수행했다. 문자열 타입은 전환하지 않았고 중복 wstring 형식 표기 1개가 없어져 기존 감사 수치는 9,146→9,145가 됐다.

| 최종 검사 | 결과 |
|---|---|
| Debug 값 타입 모듈 | 4,035 checks, 8,192회 값 연산 할당 0 |
| Release 값 타입 모듈 | 4,034 checks |
| standalone AddressSanitizer | 4,034 checks, PASS |
| Debug Editor 빌드 | Effect 패키지 포함, static-CRT launcher 포함 PASS |
| 실제 Editor 실행 | 17명령, 2×8,192 전송, 새 GPU 프레임, Play/Stop, 장면 복원, 정상 종료 |
| 실제 DX12 Debug | 8,786 checks, 호출 스레드 풀·캐시 할당/삭제 시 해제 검사 PASS |
| 실제 DX12 Release | 8,781 checks |

최종 증거는 배치의 `native-accepted/result.json`, `editor-final/acceptance.json`, `material-final/result.json`과 `Build/value-types-editor-encoding-final-build.log`다. 소유 요소의 이동·소멸, 참조 별칭·수명 제한, 변환 생성·예외 정리, 외부 get 예외 전파, 무할당 값 연산을 검사했다. 실제 렌더 검증은 조회 시 무할당과 삭제 시 해제 회귀 검사를 포함한다.

수정 전 사본과 이전 합격 manifest를 보존했다. 변경 허용 목록 밖 기존 소스 1,385개는 해시가 유지됐다. 미추적 Effect 작업의 내용과 소유권 정책도 유지했다. 저장 컨테이너 잔여 498건, 문자열 8,383건, 함수 래퍼 264건은 별도 잔여 작업이며 전체 STL 전환 완료가 아니다. 상세 상태와 해시는 배치 `migration-batch.json` 및 `Engine/Containers/integration-source.json`에 기록한다.

이전 기록: [해시 맵 배치 전환](HashMapBatchMigration20261027.html). 구현 계약은 `Engine/Containers/values/README.md`에 있다.
