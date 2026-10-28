# 모듈 바이너리 전체 성능 재수용 — 2026-10-26

실제 측정 결과: **일반 컨테이너 82/82, 고정 MPSC 주 수용 batch8 4/4·개별 전송 추가 검사 4/4, FAA 락 프리 큐 6/6, SPSC·기존 스핀 MPMC 처리량 8/8**. 전체 수용 판정은 **PASS**다. 이 판정은 새 named module 바이너리와 이번 원시 표본에서 계산했다. 이전 헤더 바이너리의 통과 판정을 승계하지 않았다.

## 고정한 수용 기준

일반·확장 컨테이너와 고정 MPSC는 매 반복에서 custom/MSVC STL 또는 PPL 중앙값 비율 ≤1.10, 양쪽 각각 정렬 15표본 x[13]/x[1] ≤1.30이어야 한다. 일반 82개 조건은 각각 별도 프로세스에서 3회, 총 246개 프로세스로 실행했다. 고정 MPSC는 4개 조건을 3프로세스에서 실행했다. 모든 15표본을 보존했고 반복 2는 일반 조건 순서와 A/B 측정 순서를 반전했다.

FAA 확장 MPMC는 기존 최적화 보고서의 6개 조건을 5프로세스에서 PPL과 비교했다. 8B/128B 값, 생산자·소비자 1/1·4/1·4/4이며 생산자당 65,536개를 전송한다. 전송 시간과 소멸 포함 시간 비율이 각 프로세스에서 모두 ≤1.10이어야 한다. FAA의 원래 기준에는 표본 편차 한도가 없으므로 이번에도 이를 추가하거나 완화하지 않았으며, 양쪽 편차는 표에 공개한다.

## 환경과 구현 경계

Windows x64 MSVC 19.50.35730, MSVC STL 145, Release, iterator debug 0, C++23이다. 실제 구현은 Creator.Containers 모듈의 22개 파티션에서 가져온다. 기존 정확성/비교 소스의 ce 구현 include를 제거하고 import를 삽입해 같은 작업을 실행했다. 측정용 STL/PPL 사용은 비교 실행 파일에만 있다. FAA 비교 소비자는 기존과 같은 /Ob3을 사용한다.

일반 및 고정 MPSC는 이전 최종 수용과 같은 측정 스레드 CPU4/above-normal, 자체 SMT pause helper CPU5 조건이다. 자체 helper는 절대 시간을 늘리는 비교 환경 제어이며 라이브러리 최적화가 아니다. 다른 앱이나 OS 설정은 바꾸지 않았다. 일반 항목은 각 구현 100ms 목표로 독립 보정하고 각각 고정 1초 워밍업 후 15표본을 수집했다. 고정 MPSC는 OS yield 대기와 개별 전송 표본을 사용하며 생산자는 다른 물리 코어에 배치한다.

FAA/PPL 5프로세스는 기존 queue_acceptance 프로토콜 그대로 SMT helper 종료 후 실행했다. 해당 프로토콜은 affinity 고정 없이 A/B 순서를 표본마다 번갈아 사용한다. 일반 단일 스레드/고정 MPSC와 다른 측정 조건임을 구분한다. 측정 중 소스와 바이너리를 수정하거나 빌드하지 않았다.

## 일반·확장 82개 조건

아래 비율 범위는 3프로세스의 중앙값 비율 범위이고, 편차 최대는 각 프로세스 안의 x[13]/x[1] 중 최대다. 프로세스 간 절대 시간 차이를 같은 통계로 해석하지 않는다.

| 조건 | 비율 최소–최대 | 자체 편차 최대 | STL 편차 최대 | 3회 모두 수용 |
|---|---:|---:|---:|---|
| core/array_growth/23 | 0.5388–0.5477 | 1.0347 | 1.0110 | True |
| core/array_growth/24 | 0.5365–0.5634 | 1.0106 | 1.0301 | True |
| core/array_growth/4096 | 0.3314–0.6233 | 1.0252 | 1.0125 | True |
| core/array_growth/64 | 0.6282–0.6346 | 1.0157 | 1.0153 | True |
| core/array_growth/65536 | 0.2571–0.5828 | 1.0249 | 1.0414 | True |
| core/array_growth/8 | 0.1815–0.1873 | 1.0060 | 1.0106 | True |
| core/array_reserved/23 | 0.9655–0.9830 | 1.0314 | 1.0076 | True |
| core/array_reserved/24 | 0.9723–0.9780 | 1.0275 | 1.0329 | True |
| core/array_reserved/4096 | 0.9702–0.9746 | 1.0082 | 1.0252 | True |
| core/array_reserved/64 | 0.9181–0.9210 | 1.0255 | 1.0061 | True |
| core/array_reserved/65536 | 1.0087–1.0104 | 1.0088 | 1.0121 | True |
| core/array_reserved/8 | 0.9431–0.9992 | 1.0091 | 1.0115 | True |
| core/fifo_batch/23 | 0.6089–0.6125 | 1.0057 | 1.0117 | True |
| core/fifo_batch/24 | 0.6195–0.6369 | 1.0114 | 1.0111 | True |
| core/fifo_batch/4096 | 0.6348–0.6374 | 1.0101 | 1.0105 | True |
| core/fifo_batch/64 | 0.6073–0.6091 | 1.0082 | 1.0172 | True |
| core/fifo_batch/65536 | 0.5144–0.5300 | 1.0161 | 1.0406 | True |
| core/fifo_batch/8 | 0.5542–0.5569 | 1.0205 | 1.0173 | True |
| core/map_build/23 | 0.1714–0.1752 | 1.0057 | 1.0349 | True |
| core/map_build/24 | 0.1670–0.1709 | 1.0079 | 1.0340 | True |
| core/map_build/4096 | 0.1554–0.1556 | 1.0071 | 1.0425 | True |
| core/map_build/64 | 0.1237–0.1258 | 1.0152 | 1.0089 | True |
| core/map_build/65536 | 0.2202–0.2280 | 1.0556 | 1.2291 | True |
| core/map_build/8 | 0.2772–0.2782 | 1.0140 | 1.0101 | True |
| core/map_hit/23 | 0.8919–0.8994 | 1.0204 | 1.0356 | True |
| core/map_hit/24 | 0.8667–0.8674 | 1.0144 | 1.0059 | True |
| core/map_hit/4096 | 0.8000–0.8101 | 1.0114 | 1.0218 | True |
| core/map_hit/64 | 0.8910–0.9021 | 1.0399 | 1.0143 | True |
| core/map_hit/65536 | 0.5783–0.5855 | 1.0282 | 1.0253 | True |
| core/map_hit/8 | 0.8558–0.8582 | 1.0096 | 1.0365 | True |
| core/map_miss/23 | 0.8169–0.8203 | 1.0089 | 1.0066 | True |
| core/map_miss/24 | 0.7550–0.7553 | 1.0077 | 1.0055 | True |
| core/map_miss/4096 | 0.5653–0.5692 | 1.0061 | 1.0508 | True |
| core/map_miss/64 | 0.8453–0.8458 | 1.0087 | 1.0070 | True |
| core/map_miss/65536 | 0.3334–0.3368 | 1.0072 | 1.0327 | True |
| core/map_miss/8 | 0.8278–0.8363 | 1.0295 | 1.0375 | True |
| core/string_append/23 | 0.5832–0.5895 | 1.0218 | 1.0266 | True |
| core/string_append/24 | 0.9882–1.0101 | 1.0112 | 1.0064 | True |
| core/string_append/4096 | 0.6561–0.6624 | 1.0096 | 1.0193 | True |
| core/string_append/64 | 0.7295–0.7403 | 1.0074 | 1.0060 | True |
| core/string_append/65536 | 0.6664–0.6738 | 1.0389 | 1.0087 | True |
| core/string_append/8 | 0.9912–0.9990 | 1.0073 | 1.0139 | True |
| core/string_copy/23 | 0.2228–0.2243 | 1.0333 | 1.0066 | True |
| core/string_copy/24 | 0.9881–1.0636 | 1.0243 | 1.0175 | True |
| core/string_copy/4096 | 1.0028–1.0089 | 1.0349 | 1.0058 | True |
| core/string_copy/64 | 0.9894–1.0202 | 1.0190 | 1.0234 | True |
| core/string_copy/65536 | 0.9978–1.0030 | 1.0102 | 1.0232 | True |
| core/string_copy/8 | 0.9057–0.9070 | 1.0225 | 1.0067 | True |
| core/string_find/23 | 0.9152–0.9189 | 1.0113 | 1.0071 | True |
| core/string_find/24 | 0.9582–0.9659 | 1.0061 | 1.0417 | True |
| core/string_find/4096 | 0.1736–0.1745 | 1.0330 | 1.0041 | True |
| core/string_find/64 | 0.5362–0.5416 | 1.0242 | 1.0132 | True |
| core/string_find/65536 | 0.1698–0.1707 | 1.0091 | 1.0073 | True |
| core/string_find/8 | 0.6988–0.7059 | 1.0228 | 1.0084 | True |
| core/string_view_construct/23 | 0.2615–0.2637 | 1.0033 | 1.0233 | True |
| core/string_view_construct/24 | 0.9936–1.0045 | 1.0257 | 1.0060 | True |
| core/string_view_construct/4096 | 1.0024–1.0127 | 1.0322 | 1.0301 | True |
| core/string_view_construct/64 | 1.0013–1.0170 | 1.0070 | 1.0259 | True |
| core/string_view_construct/65536 | 0.9973–1.0030 | 1.0225 | 1.0068 | True |
| core/string_view_construct/8 | 0.9282–0.9289 | 1.0040 | 1.0126 | True |
| expanded/age_expiration/256 | 0.8139–0.8214 | 1.0208 | 1.0150 | True |
| expanded/age_expiration/4096 | 0.0994–0.1015 | 1.0365 | 1.0220 | True |
| expanded/atom_duplicate/256 | 0.7653–0.7738 | 1.0270 | 1.0264 | True |
| expanded/atom_duplicate/4096 | 0.6820–0.6851 | 1.0173 | 1.0221 | True |
| expanded/fixed_arena_churn/256 | 0.5422–0.5643 | 1.0082 | 1.0064 | True |
| expanded/fixed_arena_churn/4096 | 0.5409–0.5418 | 1.0290 | 1.0068 | True |
| expanded/function_dispatch/256 | 1.0771–1.0897 | 1.0102 | 1.0224 | True |
| expanded/function_dispatch/4096 | 0.8871–0.8976 | 1.0124 | 1.0073 | True |
| expanded/lru_touch/256 | 0.2806–0.3121 | 1.0405 | 1.0330 | True |
| expanded/lru_touch/4096 | 0.3311–0.3330 | 1.0073 | 1.0167 | True |
| expanded/owned_array/256 | 0.9511–0.9633 | 1.0077 | 1.0083 | True |
| expanded/owned_array/4096 | 0.9072–0.9547 | 1.0201 | 1.0313 | True |
| expanded/radix_sort_u64/256 | 0.8507–0.8846 | 1.0913 | 1.0407 | True |
| expanded/radix_sort_u64/4096 | 0.4193–0.4212 | 1.0068 | 1.0063 | True |
| expanded/sequence_window/256 | 0.0585–0.0590 | 1.0245 | 1.0089 | True |
| expanded/sequence_window/4096 | 0.0351–0.0351 | 1.0060 | 1.0206 | True |
| expanded/slot_lifetime/256 | 0.2039–0.2086 | 1.0326 | 1.0321 | True |
| expanded/slot_lifetime/4096 | 0.1779–0.1804 | 1.0063 | 1.0137 | True |
| expanded/string_map_build/256 | 0.6417–0.6480 | 1.0404 | 1.0298 | True |
| expanded/string_map_build/4096 | 0.6981–0.7021 | 1.0056 | 1.0101 | True |
| expanded/string_map_hit/256 | 0.7439–0.7509 | 1.0438 | 1.0294 | True |
| expanded/string_map_hit/4096 | 0.6916–0.6938 | 1.0111 | 1.0200 | True |

### 배열 프로세스별 절대 시간 범위

표본 내부가 안정적이어도 새 프로세스의 heap/주소·상태 등에 따라 절대 중앙값이 달라질 수 있다. 아래는 각 반복의 ns/작업 중앙값 범위이며 한도 1.30이 적용되는 통계와 다르다. 이 차이의 개별 원인은 이번 비교만으로 확정하지 않는다.

| 배열 조건 | 자체 ns 최소–최대 | STL ns 최소–최대 | 자체/STL 프로세스 max/min |
|---|---:|---:|---:|
| core/array_growth/23 | 5213–5423 | 9676–9900 | 1.040/1.023 |
| core/array_growth/24 | 5229–5428 | 9566–9771 | 1.038/1.021 |
| core/array_growth/4096 | 137546–150010 | 239534–422773 | 1.091/1.765 |
| core/array_growth/64 | 8773–8889 | 13967–14009 | 1.013/1.003 |
| core/array_growth/65536 | 1653775–2132906 | 3354325–8295350 | 1.290/2.473 |
| core/array_growth/8 | 1090–1117 | 5963–6013 | 1.025/1.008 |
| core/array_reserved/23 | 1411–1420 | 1445–1467 | 1.006/1.015 |
| core/array_reserved/24 | 1430–1440 | 1462–1481 | 1.007/1.013 |
| core/array_reserved/4096 | 98364–98466 | 101034–101468 | 1.001/1.004 |
| core/array_reserved/64 | 2344–2346 | 2546–2554 | 1.001/1.003 |
| core/array_reserved/65536 | 1576880–1580317 | 1561905–1566684 | 1.002/1.003 |
| core/array_reserved/8 | 1080–1106 | 1097–1145 | 1.024/1.044 |

## 고정 MPSC 대 PPL

기존 주 수용 조건 yield-batch8도 같은 모듈 바이너리로 3회 실행했다. 이전 주 수용 프로토콜과 같이 SMT helper 없이 CPU4 측정 스레드와 분리한 생산자 코어를 사용한다. 3개 고정 워밍업 묶음, 정식 15개 묶음 각각 하위 전송 8개이며 그 평균 시간을 쓴다. 연속 단일 wall interval이나 단일 전송 지연 안정성을 뜻하지 않는다. 하위 전송 1,440쌍 전부를 보존하고 묶음 평균을 재계산했다. 진단용 pause 모드로 바꾸지 않았다.

| 주 수용 조건/용량/생산자 | 비율 최소–최대 | 자체 편차 최대 | PPL 편차 최대 | 3회 모두 수용 |
|---|---:|---:|---:|---|
| batch8/1024/1 | 0.7270–0.7724 | 1.0575 | 1.0451 | True |
| batch8/1024/4 | 0.8929–0.9393 | 1.0479 | 1.0479 | True |
| batch8/64/1 | 0.7085–0.7651 | 1.0787 | 1.0443 | True |
| batch8/64/4 | 0.9074–0.9464 | 1.0777 | 1.1256 | True |

아래는 별도 SMT 제어 조건에서의 개별 전송 추가 검사다. 주 수용 batch8과 결과를 혼합하지 않는다.

| 개별 용량/생산자 | 비율 최소–최대 | 자체 편차 최대 | PPL 편차 최대 | 3회 모두 수용 |
|---|---:|---:|---:|---|
| 1024/1 | 0.7381–0.8027 | 1.1009 | 1.1653 | True |
| 1024/4 | 0.6642–0.7041 | 1.1361 | 1.0463 | True |
| 64/1 | 0.7415–0.8032 | 1.1725 | 1.0756 | True |
| 64/4 | 0.8489–0.8726 | 1.1786 | 1.0290 | True |

## FAA 확장 MPMC 대 PPL

| 값 B/생산자/소비자 | 전송 비율 최소–최대 | 소멸 포함 비율 최대 | 자체/PPL 편차 최대 | 5회 모두 수용 |
|---|---:|---:|---:|---|
| 128/1/1 | 0.3897–0.4185 | 0.4502 | 1.2663/1.2281 | True |
| 128/4/1 | 0.6130–0.6599 | 0.7481 | 1.2195/1.0872 | True |
| 128/4/4 | 0.6306–0.6703 | 0.7365 | 1.1970/1.1234 | True |
| 8/1/1 | 0.4618–0.5365 | 0.5531 | 1.3665/1.2225 | True |
| 8/4/1 | 0.9878–1.0455 | 1.0904 | 1.1527/1.1037 | True |
| 8/4/4 | 0.5684–0.6458 | 0.6641 | 1.1471/1.2936 | True |

## SPSC 및 기존 스핀 MPMC 추가 비교

과거 concurrent 프로토콜도 빠짐없이 실행했다. 생산자당 16,000개, 2회 워밍업·9표본·3프로세스이며 아래 판정은 각 반복 처리량 비율 ≤1.10이다. 기존 보고서와 같이 x[7]/x[1]>1.25는 별도 편차 경고다. 스핀 MPMC는 락 프리 타입이 아니며 SPSC도 역할 고정 큐이므로 PPL 전체의 대체 가능성을 뜻하지 않는다.

| 타입/값 B/생산자/소비자 | 비율 최소–최대 | 자체/PPL 편차 최대 | 처리량 3회 모두 수용 |
|---|---:|---:|---|
| mpmc/128/1/1 | 0.4475–0.5481 | 1.7785/1.1060 | True |
| mpmc/128/4/1 | 0.8309–1.0119 | 1.4535/1.0688 | True |
| mpmc/128/4/4 | 0.5151–0.6240 | 1.4191/1.1413 | True |
| mpmc/8/1/1 | 0.3173–0.3793 | 1.1996/1.1276 | True |
| mpmc/8/4/1 | 0.5172–0.5301 | 1.1047/1.0532 | True |
| mpmc/8/4/4 | 0.2496–0.3217 | 1.0692/1.2886 | True |
| spsc/128/1/1 | 0.2190–0.2235 | 1.1656/1.1311 | True |
| spsc/8/1/1 | 0.1774–0.1978 | 1.2137/1.2669 | True |

## 미수용 항목과 판단

이번 범위의 모든 조건이 기존 기준을 충족했다.

## 증거와 재현

독립 라이브러리 results/20261026-module-opt-accept1-verdict.json이 판정 정본이며, 20261026-module-opt-accept1-before.json과 20261026-module-opt-accept1-environment.json에 소스·스크립트·바이너리 SHA256을 보존한다. guard JSON과 246개 case 파일의 시간 범위를 대조하고 원시 표본에서 요약 비율과 편차를 다시 계산했다. 전체 표본 파일의 해시도 판정 JSON에 기록한다.

총 324개 조건/프로세스 결과다. 일반 246·고정 MPSC 개별 12·FAA 30개 결과는 각각 15표본, SPSC·스핀 MPMC 24개 결과는 기존 방식의 9표본으로 총 4,536쌍이다. 주 수용 MPSC 12개 결과의 180개 묶음 표본(하위 전송 1,440쌍)도 추가 보존했다. 워밍업은 이 수에 포함하지 않는다. 컴파일러 및 실제 비교한 STL/PPL 설치 파일 해시도 reference-files JSON에 남겼다.

재현은 새 Tag와 별도 GuardTag로 experiments/20261026-small-array/with-guard.ps1 안에서 run-module-acceptance.ps1을 실행한다. guard 태그는 측정 태그와 다른 접두사를 사용한다. 이어 module_queue_acceptance.exe를 guard 없이 5회 실행해 <Tag>-ppl-1.csv…5.csv, module_concurrent_tests.exe를 3회 실행해 <Tag>-concurrent-1.csv…3.csv로 보존한다. 별도 run-module-mpsc-primary.ps1 -Tag <Tag>로 주 수용 batch8을 수집하고 verify-module-acceptance.ps1 -Tag <Tag> -GuardTag <GuardTag>로 검증한다. 수집 전에 reference-files JSON과 module-concurrent-before JSON에 비교 설치 파일 및 concurrent 실행 파일 해시를 기록한다. 빌드는 수집 전에 modules/validate-msvc.ps1을 실행한 뒤 module_library_benchmark·module_mpsc_benchmark·module_queue_acceptance 타깃을 Release로 빌드한다.

모듈 정확성·ASan·Linux 실행 검증은 [모듈 구현 보고서](ContainerModules20261026.md), 이전 헤더 수용은 [이전 수용 보고서](SmallArrayVarianceAcceptance20261026.md)를 참조한다. 다른 OS의 성능 동등성이나 실제 엔진 프레임 수용을 입증한 결과는 아니다. 엔진 통합과 PPL 별칭 변경은 수행하지 않았다.
