# CreatorContainers C++23 모듈 구현

2026-10-26. 독립 라이브러리 `C:/Users/idene/source/repos/CreatorContainers`의 구현 22개를 `.ixx` 인터페이스 파티션으로 이전했다. 문자열, 배열, 해시, 자원 할당기, 보조 자료구조, SPSC/MPMC 및 고정 용량 MPSC를 모두 포함한다. 엔진 솔루션과 PPL 별칭은 아직 변경하지 않았다.

## 구현과 사용

`modules/Creator.Containers.ixx`가 `export module Creator.Containers;`와 22개 파티션의 재수출을 제공한다. 각 파티션 export 블록에 실제 구현이 들어 있다. 기존 구현 헤더를 전역 조각에 포함하는 래퍼 방식이 아니다. 런타임 및 intrinsic 헤더만 전역 조각에서 읽으며 `import std`나 STL 컨테이너에 의존하지 않는다.

```cpp
import Creator.Containers;
ce::dynamic_array<int> values;
ce::string name("CreatorEngine");
```

CMake 3.28 이상, C++23에서 `CE_MODULES=ON`과 `CreatorContainers::Modules`를 사용한다. MSVC에서는 기본 활성화한다. IFC/PCM은 컴파일러와 구성별로 생성한다. 기존 헤더는 `.ixx` 정본에서 생성하는 호환 산출물이며 `modules/sync-headers.ps1 -Check`와 CMake 빌드가 일치 여부를 검사한다. 헤더 타깃과 모듈 타깃의 동시 연결은 CMake에서 거부한다. 직접 두 API를 혼합하는 수동 빌드는 지원하지 않는다.

소비자 람다 소멸자를 판정하는 MSVC IFC 문제를 실제 컴파일에서 발견했다. MSVC에서는 `__is_nothrow_destructible` 내장 판정을 사용하고 다른 컴파일러에서는 기존 noexcept 식을 유지한다. 생성 헤더에도 동일 변경을 적용했다.

## 실제 검증

| 환경 | 결과 | 범위 |
|---|---|---|
| MSVC 19.50 Release, VS 18 2026, CMake 4.3.1 | 9/9 통과 | import 단독, 일반/확장 컨테이너, 고정 MPSC, concurrent, FAA, 정렬, 교차 번역 단위, 다른 모듈 소비 |
| MSVC Release 헤더 호환 경로 포함 | 19/19 통과 | 모듈 9개 및 기존 헤더 경로 10개 |
| 같은 MSVC AddressSanitizer | 9/9 통과 | 같은 실행 검사 |
| WSL Ubuntu Clang 18.1.3 | 7/7 통과 | 22개 파티션과 기본 모듈 PCM/오브젝트 생성, import 단독 및 6개 정확성 실행 |
| 헤더/모듈 혼합 연결 음성 검사 | 예상대로 구성 거부 | CMake 호환 인터페이스 속성 |

교차 번역 단위 검사에서는 default resource 주소, 문자열 소유권 전달, 예외 catch와 FAA 참여자 등록의 공유 상태를 확인했다. 별도 named module에서 API를 재수출하고 문자열과 MPSC를 전달하는 검사도 실행했다. Linux는 설치 환경에 clang-scan-deps가 없어 명시적인 PCM 의존성 매핑으로 검증했으며 CMake 모듈 스캐너 경로를 검증한 결과는 아니다.

전체 Release 로그는 `results/20261026-modules-all-tests.txt`다. 로그는 독립 라이브러리의 `results/20261026-modules-release-tests.txt`, `20261026-modules-asan-tests.txt`, `20261026-modules-linux2.txt`, `20261026-modules-mixed.txt`에 보존했다. Linux 첫 시도의 PCM 오브젝트 의존성 전달 실패도 `20261026-modules-linux.txt`에 보존한다. 재현 스크립트는 `modules/validate-msvc.ps1 -Tag <새태그>` 및 `-AddressSanitizer`, `modules/validate-linux.ps1 -Tag <새태그>`다.

## 성능과 통합 경계

이전 전체 82/82 및 MPSC 4/4 성능 수용은 [소형 배열 편차 보고서](SmallArrayVarianceAcceptance20261026.md)의 헤더 바이너리에 대한 결과다. 모듈의 타입 소유권과 코드 생성이 달라질 수 있으므로 그 판정을 자동 승계하지 않는다.

모듈 바이너리의 `array_growth,64`를 기존 SMT 제어 조건, CPU4 측정/CPU5 자체 pause helper, 독립 묶음 100ms·15표본으로 실제 측정했다. STL 대비 시간 비율 **0.6477**, custom spread **1.0051**, STL spread **1.0055**로 이 항목은 기존 기준 1.10/1.30을 충족했다. 원시는 `results/20261026-module-array-growth64.txt`, 환경은 같은 태그의 guard JSON이다. 잘못된 항목 이름으로 표본이 없었던 앞선 `20261026-module-array64.txt`는 판정에서 제외했다.

전체 모듈 성능 재수용, 다른 OS의 성능 동등성과 실제 엔진 통합 검증은 남아 있다. 모듈 구현 완료와 이 검증 경계를 구분한다. 독립 라이브러리 [설계 문서](../design/ContainerLibraryDesign.md)를 함께 참조한다.

## 후속 전체 성능 재수용 결과

모듈 바이너리의 전체 비교를 실제 수행했다. 일반 79/82, 고정 MPSC 주 수용 batch8 4/4·개별 전송 추가 검사 3/4, FAA 5/6으로 전체 판정은 FAIL이다. SPSC·기존 스핀 MPMC의 추가 처리량 비교는 8/8 통과했다. 실패 항목·반복별 수치·측정 조건은 [전체 재수용 보고서](ModulePerformanceAcceptance20261026.md)를 참조한다. 위 구현 완료 시점의 단일 항목 확인과 구분한다.

## 모듈 최적화 후 최종 재수용

명시적 inline 처리 후 모듈 바이너리 전체 재수용은 PASS다. 일반 82/82, MPSC 주 수용 및 개별 전송 각 4/4, FAA 6/6, SPSC·스핀 MPMC 처리량 8/8을 확인했다. [원인 분리와 최적화](ModuleOptimizationDiagnostics20261026.md), [전체 수용 표](ModulePerformanceOptimization20261026.md)를 참조한다. 위 초기 미수용 결과와 원시는 이력으로 보존한다.
