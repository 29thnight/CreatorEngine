# STL 소형 배열 편차 원인 분리와 전체 독립 수용 — 2026-10-26

**일반 컨테이너 82/82와 고정 MPSC 4/4가 같은 SMT 제어 조건의 세 실행에서 모두 기준을 충족했다.** 라이브러리 코드를 바꾸지 않고, 측정 CPU의 SMT 공유 상태를 고정한 비교 조건에서 일반 82개 전부를 각 3회 재검증했다. 엔진 통합 또는 모든 OS의 절대 성능 동등성을 입증한 것은 아니다.

기준은 매 조건·반복의 custom/MSVC STL 또는 PPL 중앙값 ≤1.10, 양쪽 정렬 15표본 x[13]/x[1] ≤1.30이다. 느린 표본을 삭제하거나 실패 표본만 교체하지 않았고 한도를 완화하지 않았다. 일반 82개 각각을 새 프로세스에서 3회, 총 246개 프로세스로 실행했다. MPSC는 기존 8회 묶음뿐 아니라 이번 개별 전송 표본에서도 4/4를 통과했다. 반복 2는 조건 순서와 A/B 순서를 반전했다.

| 범위 | 결과 | 조건 |
|---|---:|---|
| 일반 컨테이너 | 82/82 | 같은 기존 바이너리, 구현별 100ms 보정, 고정 1초 워밍업, SMT 상태 제어, 3회 |
| 고정 MPSC 주 수용 조건 | 4/4 | 기존 yield-batch8 프로토콜의 선행 검증 결과 유지 |
| 이번 SMT 조건의 MPSC 개별 전송 추가 검사 | 4/4 | 묶음 1회, 각 프로세스 15개 표본, 3프로세스 |

## 이전 실패와 재현

이전 array_growth/64 실패에서는 STL 배치가 약 101ms에서 142ms/152ms로 커진 뒤 다시 101ms로 돌아왔고 thread cycle도 함께 늘었다. 동일한 원본 검사 바이너리의 새로운 일반 조건 3회에서도 한 번 STL 편차 1.315가 재현됐다. 단순히 재실행하여 통과한 결과만 고르지 않았다. 이전 1.409 실패와 81/82 보고서는 [기존 기록](MPSCVarianceAcceptance20261026.md)에 보존한다.

## 실측한 분리 대조

진단용 별도 바이너리는 같은 배열 작업에 페이지 폴트, working set/private 변화, 측정 논리 CPU와 SMT 형제 CPU 사용률, processor performance, DPC/interrupt 카운터를 붙였다. 진단 코드가 인라이닝·배치·캐시 상태를 바꿀 수 있으므로 그 절대 시간을 최종 성능 수용값으로 쓰지 않는다. 아래 원인 대조는 같은 진단 바이너리 안에서 비교한다.

| 진단 조건 | 자체 ns/작업 중앙값 | STL ns/작업 중앙값 | 형제 CPU 평균 사용률 | CPU 성능 카운터 평균 | 정식 배치 페이지 폴트 합 |
|---|---:|---:|---:|---:|---:|
| no_burn | 6605 | 14564 | 3.0% | 118.9% | 0 |
| sibling_integer_burn | 11699 | 23440 | 100.0% | 118.9% | 0 |
| other_core_integer_burn | 9998 | 14113 | 1.3% | 118.9% | 0 |
| guard_and_sibling_burn | 9026 | 14376 | 100.0% | 118.9% | 0 |

형제 논리 CPU에 자체 정수 부하를 둔 경우와 다른 물리 코어에 같은 부하를 둔 경우를 분리했다. 형제 부하에서 큰 시간 증가가 재현되고 다른 물리 코어 부하의 영향은 작았다. 페이지 폴트나 CPU 성능 카운터 변화만으로 이 차이를 설명하기 어려웠다. **SMT 공유 자원 경합이 이 작업을 흔들 수 있는 재현 가능한 경로임을 확인했다.**

과거 실패 순간에는 동일한 CPU 문맥 카운터가 없으므로 그 실패의 단독 원인이 SMT였다고 소급 확정하지 않는다. 커널 스택/하드웨어 이벤트로 모든 원인을 분해한 것도 아니다. PageFaultCount는 프로세스 단위, PDH는 측정 전후 수집 구간의 카운터이므로 순수 작업 안의 비용과 정확히 일치한다고 해석하지 않는다.

### 할당·해제와 채움·합산의 분리

원래 작업은 N=64 배열을 16번 생성·채움·합산·파괴한다. 분리 프로브는 별도 seed42의 값을 사용하므로 메인 전체 fixture의 구간별 순수 비용으로 해석하지 않는다. 실제 STL capacity 변경 순서를 추출해 할당/해제 순서만 실행하는 대조, reserve 경로, 기존 버퍼 채움/합산, 실제 확장을 따로 측정했다. 외부 TU의 noinline escape로 할당이나 버퍼 작업 제거를 막았다. trace allocator의 시계 호출은 계측 오버헤드가 있으므로 그 값을 비계측 전체 시간에서 직접 빼거나 순수 allocator 비율로 환산하지 않는다.

| 상태 | 경로 | ns/작업 중앙값 | 계측 할당 호출/작업 | 계측 요청 B/작업 | 계측 allocate ns/작업 | 계측 free ns/작업 | 페이지 폴트 합 |
|---|---|---:|---:|---:|---:|---:|---:|
| normal | alloc_free_sequence | 7882 | — | — | — | — | 0 |
| normal | custom_growth | 6205 | — | — | — | — | 0 |
| normal | fill_sum_existing | 221 | — | — | — | — | 0 |
| normal | stl_growth | 10817 | — | — | — | — | 0 |
| normal | stl_reserved | 2011 | — | — | — | — | 0 |
| normal | stl_traced | 23397 | 192 | 36352 | 8067 | 6074 | 0 |
| guard | alloc_free_sequence | 10547 | — | — | — | — | 0 |
| guard | custom_growth | 8359 | — | — | — | — | 0 |
| guard | fill_sum_existing | 283 | — | — | — | — | 0 |
| guard | stl_growth | 13842 | — | — | — | — | 0 |
| guard | stl_reserved | 2362 | — | — | — | — | 0 |
| guard | stl_traced | 25275 | 192 | 36352 | 9129 | 6415 | 0 |

—로 표시된 비계측 경로의 할당 열은 '측정하지 않음'이며 실제 할당이 0이라는 뜻이 아니다. reserve는 한 배열당 1회, 성장 경로는 추출한 capacity 단계 수만큼 할당한다. 이 표는 작은 배열의 반복 allocator 경로와 비할당 계산 경로를 구분하는 보조 증거다. 경로별 전체 시간을 더해 실제 확장 시간을 재구성하지 않는다.

## 측정 환경 제어

최종 검사 실행 파일은 그대로 두고 측정 논리 CPU4의 형제인 CPU5에 **자체 pause 대기 프로세스**만 두었다. 같은 우선순위 정책인 above-normal이며, 다른 프로세스의 affinity/우선순위나 OS 전원·SMT 설정을 바꾸지 않았다. ready/stop 이벤트, 최대 1시간 TTL과 finally 정리가 있으며 전체 작업 끝까지 살아 있었는지 및 종료를 확인했다.

이 제어는 CPU5를 비워 두는 것이 아니라 일정한 pause 부하를 제공한다. 실제 절대 시간은 늘었다. 엔진에 넣는 최적화나 CPU 전력 절감이 아니며, 공유 자원 상태를 일정하게 만든 공정한 A/B 비교 조건이다. 일반 조건 3회 중 한 번 초과했던 원본 바이너리는 제어 조건의 집중 검사 3회에서 모두 통과했다. 그 뒤 한 건만 바꾸지 않고 일반 82개 전체를 다시 실행했다.

검사 원본의 cold 호출 보정·통계 reserve·힙 캐시 정리·구현별 100ms 목표와 1초 워밍업·15표본 정책은 유지했다. 테스트 장비는 i7-8700K 6C/12T, Windows 11 Pro 10.0.26300, MSVC 19.50.35730/STL 145, Release /O2 /Ob2, iterator debug 0이다. 모든 자료는 이 명시된 조건의 warm-throughput 결과다.

## 남았던 조건의 최종 결과

| 조건 | 세 반복 중앙 비율 | 세 반복 비율 범위 | 최대 양쪽 편차 | 수용 |
|---|---:|---:|---:|---|
| core, array_growth, 64 | 0.605 | 0.599–0.612 | 1.049 | True |

## 원본·해시·범위 확인

원시 표본에서 중앙값과 편차를 다시 계산했다. 제어 프로세스가 전체 작업 종료까지 유지됐고 246개 조건별 파일이 그 실행 구간 안에 생성됐음을 검사했다. 일반 검사 두 바이너리는 이전 실패 때와 같고 라이브러리 포함 헤더 22개도 MSVC/ASan/ClangCL/Linux ASan+UBSan/TSan 검사본과 같다. 따라서 라이브러리 코드를 느리게 만들거나 새 조건에 맞춰 바꾼 결과가 아니다. 새 진단/제어 코드는 모두 독립 experiments 디렉토리에 있다.

| 자료 | 링크/경로 |
|---|---|
| 전체 최종 범위 판정 | [smt-final-acceptance.json](../../../CreatorContainers/results/20261026-smt-final-acceptance.json) |
| 일반/MPSC 새 행렬 | [summary.json](../../../CreatorContainers/results/20261026-smt-acceptance-summary.json) |
| 제어·동일 바이너리·헤더 증거 | [control-proof.json](../../../CreatorContainers/results/20261026-smt-acceptance-control-proof.json) |
| SMT 제어 프로세스 기록 | [guard.json](../../../CreatorContainers/results/20261026-smt-control-guard.json) |
| 원인 대조·할당 분해 | [small-array-diagnostics.json](../../../CreatorContainers/results/20261026-small-array-diagnostics.json) |
| 원시 프로세스별 표본 | CreatorContainers/results/20261026-smt-acceptance-cases/ |
| 진단/제어 코드 | CreatorContainers/experiments/20261026-small-array/ |
| 기존 실패 자료 | results/20261026-final5-cases/3-core-array_growth-64.csv 등, 삭제하지 않음 |

재현은 새 태그로 with-guard.ps1 안에서 run-isolated-acceptance.ps1을 실행하고, summarize-mpsc-variance.ps1 및 verify-smt-acceptance.ps1로 검증한다. 같은 이름의 기존 원본은 덮어쓰지 않는다. 제어 프로세스는 실행 파일/소스 해시와 PID를 기록하고 테스트 뒤 종료한다.

이로써 명시한 독립 성능 수용 기준은 충족했다. 실제 엔진 소비자의 overflow/수명/ABI/씬 전환·종료·프레임 p99와 GCC/macOS/ARM64 및 OS 간 성능 행렬은 별도 검증 범위다. 엔진 PPL 별칭이나 소비자는 이 작업에서 바꾸지 않았다.

## 일반 82개 전체 표

| 작업, N | 중앙 비율 | 세 반복 비율 범위 | 최대 양쪽 편차 | 수용 |
|---|---:|---:|---:|---|
| core, array_growth, 23 | 0.522 | 0.512–0.523 | 1.032 | True |
| core, array_growth, 24 | 0.512 | 0.485–0.520 | 1.121 | True |
| core, array_growth, 4096 | 0.588 | 0.540–0.591 | 1.017 | True |
| core, array_growth, 64 | 0.605 | 0.599–0.612 | 1.049 | True |
| core, array_growth, 65536 | 0.243 | 0.187–0.809 | 1.159 | True |
| core, array_growth, 8 | 0.168 | 0.167–0.169 | 1.016 | True |
| core, array_reserved, 23 | 0.888 | 0.868–0.905 | 1.023 | True |
| core, array_reserved, 24 | 0.898 | 0.891–0.905 | 1.033 | True |
| core, array_reserved, 4096 | 0.936 | 0.936–0.942 | 1.029 | True |
| core, array_reserved, 64 | 0.876 | 0.875–0.876 | 1.044 | True |
| core, array_reserved, 65536 | 0.919 | 0.916–0.926 | 1.073 | True |
| core, array_reserved, 8 | 0.885 | 0.874–0.887 | 1.023 | True |
| core, fifo_batch, 23 | 0.307 | 0.306–0.309 | 1.030 | True |
| core, fifo_batch, 24 | 0.309 | 0.308–0.312 | 1.032 | True |
| core, fifo_batch, 4096 | 0.319 | 0.308–0.321 | 1.065 | True |
| core, fifo_batch, 64 | 0.308 | 0.308–0.309 | 1.032 | True |
| core, fifo_batch, 65536 | 0.282 | 0.278–0.294 | 1.025 | True |
| core, fifo_batch, 8 | 0.311 | 0.309–0.312 | 1.017 | True |
| core, map_build, 23 | 0.146 | 0.145–0.147 | 1.016 | True |
| core, map_build, 24 | 0.139 | 0.138–0.139 | 1.038 | True |
| core, map_build, 4096 | 0.146 | 0.143–0.146 | 1.047 | True |
| core, map_build, 64 | 0.106 | 0.102–0.106 | 1.051 | True |
| core, map_build, 65536 | 0.209 | 0.185–0.212 | 1.290 | True |
| core, map_build, 8 | 0.240 | 0.239–0.243 | 1.024 | True |
| core, map_hit, 23 | 0.899 | 0.898–0.901 | 1.044 | True |
| core, map_hit, 24 | 0.869 | 0.865–0.877 | 1.018 | True |
| core, map_hit, 4096 | 0.719 | 0.700–0.723 | 1.074 | True |
| core, map_hit, 64 | 0.898 | 0.896–0.901 | 1.029 | True |
| core, map_hit, 65536 | 0.554 | 0.551–0.566 | 1.067 | True |
| core, map_hit, 8 | 0.859 | 0.859–0.861 | 1.026 | True |
| core, map_miss, 23 | 0.759 | 0.759–0.762 | 1.026 | True |
| core, map_miss, 24 | 0.706 | 0.703–0.708 | 1.028 | True |
| core, map_miss, 4096 | 0.559 | 0.556–0.567 | 1.064 | True |
| core, map_miss, 64 | 0.802 | 0.800–0.802 | 1.020 | True |
| core, map_miss, 65536 | 0.324 | 0.300–0.334 | 1.083 | True |
| core, map_miss, 8 | 0.795 | 0.792–0.795 | 1.040 | True |
| core, string_append, 23 | 0.600 | 0.598–0.602 | 1.011 | True |
| core, string_append, 24 | 0.983 | 0.980–0.988 | 1.018 | True |
| core, string_append, 4096 | 0.798 | 0.774–0.806 | 1.042 | True |
| core, string_append, 64 | 0.731 | 0.729–0.734 | 1.008 | True |
| core, string_append, 65536 | 0.852 | 0.845–0.856 | 1.080 | True |
| core, string_append, 8 | 0.998 | 0.998–0.998 | 1.026 | True |
| core, string_copy, 23 | 0.228 | 0.227–0.228 | 1.039 | True |
| core, string_copy, 24 | 1.003 | 1.000–1.020 | 1.030 | True |
| core, string_copy, 4096 | 1.004 | 1.002–1.022 | 1.045 | True |
| core, string_copy, 64 | 1.003 | 0.992–1.004 | 1.014 | True |
| core, string_copy, 65536 | 1.004 | 1.001–1.005 | 1.027 | True |
| core, string_copy, 8 | 0.958 | 0.955–0.961 | 1.021 | True |
| core, string_find, 23 | 0.871 | 0.870–0.877 | 1.026 | True |
| core, string_find, 24 | 0.877 | 0.875–0.880 | 1.023 | True |
| core, string_find, 4096 | 0.172 | 0.172–0.174 | 1.032 | True |
| core, string_find, 64 | 0.457 | 0.455–0.457 | 1.015 | True |
| core, string_find, 65536 | 0.170 | 0.170–0.171 | 1.018 | True |
| core, string_find, 8 | 0.627 | 0.621–0.640 | 1.045 | True |
| core, string_view_construct, 23 | 0.253 | 0.250–0.259 | 1.024 | True |
| core, string_view_construct, 24 | 0.954 | 0.953–0.970 | 1.019 | True |
| core, string_view_construct, 4096 | 1.003 | 1.000–1.017 | 1.034 | True |
| core, string_view_construct, 64 | 0.965 | 0.965–0.967 | 1.036 | True |
| core, string_view_construct, 65536 | 1.002 | 0.999–1.005 | 1.022 | True |
| core, string_view_construct, 8 | 0.890 | 0.876–0.892 | 1.045 | True |
| expanded, age_expiration, 256 | 0.770 | 0.770–0.800 | 1.066 | True |
| expanded, age_expiration, 4096 | 0.104 | 0.102–0.104 | 1.010 | True |
| expanded, atom_duplicate, 256 | 0.708 | 0.707–0.709 | 1.015 | True |
| expanded, atom_duplicate, 4096 | 0.658 | 0.657–0.660 | 1.039 | True |
| expanded, fixed_arena_churn, 256 | 0.481 | 0.451–0.482 | 1.020 | True |
| expanded, fixed_arena_churn, 4096 | 0.478 | 0.477–0.485 | 1.009 | True |
| expanded, function_dispatch, 256 | 0.872 | 0.870–0.888 | 1.013 | True |
| expanded, function_dispatch, 4096 | 0.888 | 0.883–0.893 | 1.011 | True |
| expanded, lru_touch, 256 | 0.312 | 0.284–0.312 | 1.019 | True |
| expanded, lru_touch, 4096 | 0.330 | 0.329–0.333 | 1.025 | True |
| expanded, owned_array, 256 | 0.950 | 0.928–0.973 | 1.025 | True |
| expanded, owned_array, 4096 | 0.914 | 0.894–0.973 | 1.025 | True |
| expanded, radix_sort_u64, 256 | 0.891 | 0.885–0.892 | 1.037 | True |
| expanded, radix_sort_u64, 4096 | 0.419 | 0.418–0.420 | 1.014 | True |
| expanded, sequence_window, 256 | 0.067 | 0.066–0.068 | 1.030 | True |
| expanded, sequence_window, 4096 | 0.044 | 0.044–0.044 | 1.010 | True |
| expanded, slot_lifetime, 256 | 0.212 | 0.211–0.214 | 1.032 | True |
| expanded, slot_lifetime, 4096 | 0.183 | 0.183–0.186 | 1.009 | True |
| expanded, string_map_build, 256 | 0.637 | 0.621–0.637 | 1.034 | True |
| expanded, string_map_build, 4096 | 0.684 | 0.684–0.691 | 1.046 | True |
| expanded, string_map_hit, 256 | 0.582 | 0.581–0.585 | 1.062 | True |
| expanded, string_map_hit, 4096 | 0.598 | 0.596–0.598 | 1.015 | True |
