# PHASE 4.x 공수 재산정 원장

2026-10-07 PR 감사 당시: RG6·RG-V는 병합된 소스를 반영해 progress로 표시하되 기성 0, 추정/잔여 공수는 유지한다. [감사 근거](../analysis/MergedPrReview20261007.md).

2026-10-07 후속: 최종 조합·reference·실제 Editor capture/fixture 수용을 완료해 RG5를 done, 기성 10인일로 회수했다. [최종 증거](../analysis/RenderRg5FinalAcceptance20261007.md). 이후 RG6의 별도 진단/기본 제품 D/R 독립 live 전후 비교·100회 결정성·비용·validation·정상 종료를 수용하여 기성 4인일을 추가 회수했다. [RG6 증거](../analysis/RenderRg6Acceptance20261007.md). 이후 RG-V 기본 native viewer의 D/R 실제 reader·scene/resize·변이·비용·정상 종료를 수용해 4인일을 추가 회수했다. [RG-V 증거](../analysis/GraphRecoveryRgV20261007.md). MAT-9와 C# 연결·alias/queue/range 확장·Vulkan runtime은 올리지 않는다.

**2026-10-01 사용자 요청 · 계획 추정 · 1인 전담 엔지니어 기준 인일.**

Vulkan 비교는 PHASE 4.9의 RenderDoc 작업으로만 산정한다. RHI 중립 타입·변환·capability·수명 구현을
삭제하거나 DX12 전용 설계로 되돌리는 비용 절감은 하지 않는다. 기존 소스의 LivePipelineDesc/Pass 기반,
BASE-0 snapshot/readback/변이, 단일 queue·version 미구현 표면, 현재 C# Pipeline builder 부재와 각 정본의
잔여 소비자를 근거로 작업을 나눴다. 이는 실측 소요 시간이나 확정 납기가 아니다.

## 집계 규칙

2026-10-09 구조 재감사: RG8의 제품 실행기 구조 결함으로 완료/기성 16 회수를 철회한다. RG8 progress·기성 0, 총 추정 563·기성 126·잔여 437, PHASE 4.3 기성 74·잔여 26이다. RG7의 제한된 구현/실험 종료는 유지한다. [감사 근거](../analysis/RenderRg7Rg8StructuralAudit20261009.md). 아래 RG8 종료 집계는 재감사 전 이력이다.

2026-10-09 RG8 최종 종료: 기존 D/R 큐 실행·수명·제품 수용 후 Release 336출력 오차 0·일반 GPU 192프레임·연속 DXGI 1846표본과 최신 Release 전체 RG 회귀로 채택 판정을 완료했다. 개선 미입증으로 기본 OFF를 유지하고 기존 16인일을 1회 회수한다. 총 추정 563·기성 142·잔여 421(+미산정), PHASE 4.3 기성 90·잔여 RG9 10. [근거](../analysis/RenderRg8Closure20261009.md). 아래 과거 집계는 당시 스냅샷이다.

2026-10-08 Q0 최종 종료: 고정 3단계의 DX12 D/R 수용(CPU 23·native 28·제출/수명 43·전체 RG 회귀·exit 0)을 완료하여 기존 6인일을 1회 회수했다. 현재 총 추정 563·기성 126·잔여 437(+미산정), PHASE 4.3 기성 74·잔여 26. RG8/L4 소비·RG9/Vulkan 수용은 올리지 않는다. [근거](../analysis/RhiQueueContractQ0_20261008.md).


2026-10-08 RG7 최종 종료: 기존 14인일을 1회 회수하여 당시 기성 120·잔여 443인일(+별도 미산정), PHASE 4.3 기성 68·잔여 32로 갱신한다. 총 추정 563 유지. 성능 개선/기본 ON 채택이 아닌 bounded 구현·측정·OFF 채택 판정 완료이며 RG7-H/S 후속을 완료로 세지 않는다. [근거](../analysis/RenderRg7Closure20261008.md). 이후 과거 집계 문구는 당시 스냅샷이다.

2026-10-08 [PHASE 4.85](PathTracingHybridPipelinePlan.md) 공수 산정: 공통 RT·Hybrid·PathTracing·ReSTIR PT Enhanced·tetrahedral cages·제품 수용의 RTP-0~13 14행은 중앙 추정 208인일·미착수·기성 0이다. 산정 총계 355→563, 기성 106 유지, 산정 잔여 249→457인일이며 별도 GD/Lattice 등 미산정은 남는다. 기본 환경 116에 ±30%, 효과 확장/연구 92에 ±50%를 적용한 관리 범위는 약 130~290인일이다. RTP-0/첫 prototype 뒤 GPU-9가 변경 추정을 검토한다. 기존 RT0/RT1/HY0·EXP는 RTP에 포함하며 중복 합산하지 않는다.

2026-10-03 BASE-0 4·RG1 8·RG2 10·RG3 8·RG4 6인일을 회수했고, 2026-10-07 RG5 최종 수용으로 10인일을 추가 회수했다. 총 추정 355인일은 유지하며 RG6 4·RG-V 4인일을 추가 회수해 현재 기성 106·잔여 249인일이다. 계획 공수 회수이며 실제 소요 시간 역산이 아니다.

- 완료 기반의 기존 52인일(4=18, MAT-0~6/MAT-7-BASE/MAT-8=32, L0=2)은 그대로 보존한다. 2026-10-02 MAT-7의 기존 3인일을 검증된 LX 기반 MAT-7-BASE로 옮긴 뒤 공통 재질 통합·혼합 transport·제품 회귀를 완료했다. 재개방한 MAT-7의 `days:null`/기성 추가 0은 유지한다. 진행/미착수 행의 새 값은 남은 완료 조건의 예산이다. 이미 소모한 미기록 작업 시간을 역산하지 않는다.
- 구현 범위가 같으면 유지한다. 비교 제거를 이유로 모든 행에 같은 할인율을 적용하지 않는다.
- Q0는 중립 queue/fence 기반, RG8은 graph scheduling, L4는 bake 소비다. RG-V는 기본 viewer, RG9는 range/alias/queue 확장이다. BASE-0와 BP는 각각 DX12 재현 기반과 RenderDoc 교차 조사로 나눠 중복 합산하지 않는다.
- 일반 구현 추정의 불확실성은 약 ±30%, 처음 산정한 C#/RenderDoc 통합은 약 ±40%다. 지원 SDK/하드웨어와 미확인 결함의 신규 범위는 착수 시 재산정한다. 아래는 중앙 추정치이며 달력 일정과 다르다.
- Lattice 횡단 8행, GD 전체 잔여·Vulkan RT 신규 구현(별도 산정), RTP에 포함되지 않은 추가 DXR 연구, Phase14/21 UI는 별도다. `days:null`을 0일 완료나 이 총계에 포함된 작업으로 해석하지 않는다.

## 페이즈별 비교

| 페이즈 | 이전 산정 | 새 산정 | 완료 보존 | 새 잔여 | 변화 근거 |
|---|---:|---:|---:|---:|---|
| 4 | 18 | 18 | 18 | 0 | 완료 이력 유지 |
| 4.25 | 34 | 40 + 미산정 | 32 | 8 + 미산정 | 기존 기반 보존, 공통 재질 통합 재개방과 SSS/투과·Blender/route·성능 회수 |
| 4.3 | 119 + Q0/RG-V 미산정 | 100 | 74 | 26 | BASE-0·RG1~RG7·RG-V·Q0 수용; RG8 구조 수정·RG9 잔여 |
| 4.5 | 86 | 71 | 0 | 71 | 중립 SDK 계약 유지, DX12 구현/수용, Vulkan 비교 제외 |
| 4.6 | 미산정 | 32 | 0 | 32 | native IR부터 C#/Roslyn/제품 전환까지 7행 최초 산정 |
| 4.7 | 35 | 35 | 2 | 33 | 원래 DX12 베이크 범위; UV1/BVH/취소·progressive 범위 유지 |
| 4.75 | 20 + ENV 미산정 | 28 | 0 | 28 | DX12 품질 18 + ENV 전체 route 10 |
| 4.8 | 5.5 + 구현 미산정 | 9 + 구현 미산정 | 0 | 9 + 구현 미산정 | GPU Scene/IBL/AS 공유 설계와 실제 구현 공수 확정 보강 |
| 4.85 | 별도 RT/HY/EXP 미산정 | 208 | 0 | 208 | 기본 환경 116 + Hybrid 확장 28 + ReSTIR 28 + cage 36; RT/HY/EXP 중복 제거 |
| 4.9 | 미산정 | 22 | 0 | 22 | RenderDoc 6행, 캡처/리소스/픽셀/수정/재캡처 |
| **합계** | **317.5 + 미산정** | **563 + 별도 미산정** | **126** | **437 + 별도 미산정** | **신규 산정 74 + RTP 208 - 기존 범위 조정 36.5 = +245.5; BASE-0 4·RG1 8·RG2 10·RG3 8·RG4 6·RG5 10·RG6 4·RG-V 4·RG7 14·Q0 6 회수; RG8 회수 철회** |

## ID별 재산정

| 페이즈 | ID | 이전 | 새 인일 | 상태 | 산정 근거 |
|---|---|---:|---:|---|---|
| 4 | `PBR-W0` | 2 | 2 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W1` | 1 | 1 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W2` | 2.5 | 2.5 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W3` | 1 | 1 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W4` | 2 | 2 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W5` | 2.5 | 2.5 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W6` | 1.5 | 1.5 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W7` | 2 | 2 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W8` | 2 | 2 | done | 완료된 범위와 기존 기성 보존 |
| 4 | `PBR-W9` | 1.5 | 1.5 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-0` | 2 | 2 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-1` | 4 | 4 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-2` | 4 | 4 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-3` | 4 | 4 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-4` | 4 | 4 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-5` | 4 | 4 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-6` | 4 | 4 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-7-BASE` | MAT-7의 3 | 3 | done | 기존 LX binding/generation/PSO/쿠킹의 검증된 기반 보존. 공통 통합 완료를 뜻하지 않음 |
| 4.25 | `MAT-7` | 기존 기반과 분리 | 미산정·기성 추가 0 | done | Graph→ShaderMeta/Slang·LX 공통 Material/Forward+·alpha/SSS/transmission/Volume 혼합·제품 회귀 완료. 미기록 시간 역산 및 기반 3일 중복 계상 없음 |
| 4.25 | `MAT-8` | 3 | 3 | done | 완료된 범위와 기존 기성 보존 |
| 4.25 | `MAT-9` | 2 | 8 | progress | SSS·투과 품질 잔여 4 + Blender/route 수용 2 + 실제 후속 구현 뒤 성능 재판정 2; GPU 구현 자체 제외 |
| 4.3 | `BASE-0` | 6 | 4 | done | 2026-10-03 현재 Debug/Release 구성별 2프로세스×2캡처·graph/변이·계측·validation·정상 종료 및 구성 간 이미지/현재 해시 최종 회수. 범용 재생 제외; 기성 4인일 |
| 4.3 | `RG1` | 8 | 8 | done | 명시 access·stable RAW DAG·오류 거부·24 shuffle 및 Debug/Release GPU/제품 회귀 완료. 기성 8인일; 제품 이관은 RG5/6 |
| 4.3 | `RG2` | 10 | 10 | done | version/Modify·RAW/WAR/WAW·stale/fork 거부·texture/buffer 240 shuffle 및 Debug/Release GPU/제품 회귀 완료; 기성 10인일 |
| 4.3 | `RG3` | 8 | 8 | done | version producer 컬링·WAR/WAW 재연결·sorted lifetime/Transition/UAV, 120 shuffle 및 Debug/Release GPU/제품 회귀 완료; 기성 8인일 |
| 4.3 | `RG4` | 7 | 6 | done | wave·target append·critical path, 24 shuffle·Debug/Release 1/2/4 워커 GPU·제품 16개 이미지 오차 0; 기성 6인일 |
| 4.3 | `RG5` | 12 | 10 | done | 2026-10-07 reference 및 최종 Fog/PostChain/UI/Editor 명시 접근/반환 출력·3정책 129 frames·개별 기능 수용, 독립 4프로세스/8캡처·16개 이미지 오차 0·validation 0·현재 해시 계약·14개 변이 거부. 제품 69·fixture 253 위치 감사와 임시 추론 adapter 잔여 0, 의도한 legacy 비교 보존. 기성 10인일; RG6 전환 전후 수용은 별도 |
| 4.3 | `RG6` | 8 | 4 | done | D/R legacy/기본 제품 독립 8프로세스·408캡처·각 100회 graph 결정성·final 색상 오차 0·depth 오차 기준 통과·CPU/GPU 산출물·validation 0·exit 0; 기성 4인일, Vulkan 제외 |
| 4.3 | `RG7` | 20 | 14 | done | 2026-10-08 bounded DX12 종료·기성 14인일. D/R 전체 회귀·11 GPU/6 실패/2 수명/native WARP 제거 통과. Release 독립 4실행(ON→OFF/OFF→ON)·32캡처·반복/교차 126출력 오차 0·validation/drops 0·exit 0. 연속 조회 17,684건·최대 간격 130ms·조회 실패 0, 공유 힙/캐시와 DXGI 사용량 분리. 개선 미입증으로 기본 OFF 유지·opt-in/fallback 보존. 정확한 residency/per-resource committed 귀속은 주장하지 않으며 RG7-H/S·Vulkan 4.9·Q0/RG8/RG9 별도. 근거: RenderRg7Closure20261008.md |
| 4.3 | `Q0` | 미산정 | 6 | done | 2026-10-08 중립 queue/timeline·recording 계약, DX12 queue별 제출/완료점/회수·COMMON buffer 인계·backend closed-list pin 어댑터 수용. 기존 Editor D/R CPU 23·native 28·제출/수명 43검사·전체 RG 회귀·16KiB 오차 0·validation 0·exit 0. 지연 생산자, 무관한 큰 fence 값, 사전 거절/실행 후 격리/장치 손실·종료 회수 검증. 기성 6 회수, 추정 6 유지. 제품 graph scheduling·L4 소비·Vulkan runtime은 별도. RhiQueueContractQ0_20261008.md |
| 4.3 | `RG8` | 25 | 16 | progress | 2026-10-09 구조 재감사로 완료/기성 16 회수 철회·기성 0. 기존 정확성/수명 회귀 보존. 후속 수정 1·2에서 batch·recording pool·공통 barrier 계획 적용 및 D/R 150 실행·24 계획·전체 RG 회귀 통과(RenderRg8Barriers20261009.md). 단일 큐 compiled 계획 재사용. 다중 큐 보수적 경계 전이, overlap 없는 SSAO 배치, RG7 조합 거부는 잔여. 기본 OFF 유지. 근거 RenderRg7Rg8StructuralAudit20261009.md |
| 4.3 | `RG9` | 15 | 10 | todo | range/subresource/split 7 + Inspector 확장/회귀 3; 기본 viewer는 RG-V |
| 4.3 | `RG-V` | 미산정 | 4 | done | 기본 native snapshot reader/UI·D/R 실제 view/scene/resize·변이·비용·exit 0 수용. C# IR/alias/queue/range는 후속 페이즈 |
| 4.5 | `TR0` | 4 | 3 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TR1` | 10 | 8 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TR2` | 6 | 5 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TR3` | 5 | 5 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TU0` | 4 | 3 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TU1` | 3 | 2 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TU2` | 6 | 5 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TU3` | 4 | 3 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TU4` | 3 | 3 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TU5` | 4 | 4 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `FG0` | 10 | 8 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `FG1` | 5 | 4 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `FG2` | 8 | 6 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `FG3` | 5 | 4 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `FG4` | 4 | 4 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.5 | `TFG9` | 5 | 4 | todo | DX12 구현·지원 하드웨어·실프레임 회귀로 범위 한정; 중립 SDK/시간축 계약 유지 |
| 4.6 | `CSRP-0` | 미산정 | 3 | todo | 현행 19 node/per-view/Host 소비자·DX12 fixture 실측 3 |
| 4.6 | `CSRP-1` | 미산정 | 6 | todo | immutable native IR/schema/registry 4 + negative fixture 2 |
| 4.6 | `CSRP-2` | 미산정 | 5 | todo | typed C# builder/interop 세대 게시 3 + 예외/스레드/fence 검증 2 |
| 4.6 | `CSRP-3` | 미산정 | 4 | todo | PassSchema/ShaderMeta/keyword 합성 3 + ID/ABI 변이 1 |
| 4.6 | `CSRP-4` | 미산정 | 6 | todo | Roslyn generator/analyzer·Editor metadata 4 + 생성물/실패 회귀 2 |
| 4.6 | `CSRP-5` | 미산정 | 4 | todo | 기본 19 node C# 조립 전환 3 + DX12 C++ 대비 회귀 1 |
| 4.6 | `CSRP-6` | 미산정 | 4 | todo | escape/fallback/reload/세대 수명 3 + DX12 검증 1 |
| 4.7 | `L0` | 2 | 2 | done | 완료된 범위와 기존 기성 보존 |
| 4.7 | `L1` | 5 | 5 | todo | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.7 | `L2` | 4 | 4 | todo | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.7 | `L3` | 6 | 6 | todo | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.7 | `L4` | 7 | 7 | todo | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.7 | `L5` | 5 | 5 | todo | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.7 | `L6` | 3 | 3 | todo | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.7 | `L7` | 3 | 3 | blocked | 기존 DX12 베이크 구현/수용 범위 유지; L7은 측정 뒤 채택하는 조건부 예산 |
| 4.75 | `RND-1` | 4 | 5 | todo | probe asset/capture/우선순위·specular AO 4 + DX12 golden/비용 1 |
| 4.75 | `RND-2` | 10 | 8 | todo | shadow module/atlas/tier 6 + DX12 masked·bias·fade 수용 2 |
| 4.75 | `RND-3` | 6 | 5 | todo | OETF/AgX/exposure/bloom 4 + DX12 pre-tone/표시 회귀 1 |
| 4.75 | `RND-ENV` | 미산정 | 10 | progress | ENV-A 마무리 1 + schema/resolver 2 + 전체 route weight 3 + cache/cook 2 + DX12 제품 수용 2 |
| 4.8 | `GPU-1` | 2 | 3 | progress | GPU Scene 상주·delta·material/PSO·indirect·IBL 재사용 설계 및 capability 증거 3 |
| 4.8 | `GPU-2` | 1.5 | 2 | todo | tile 후보·reuse/denoise·fallback 설계 2; 실제 구현 제외 |
| 4.8 | `GPU-3` | 1.5 | 2 | progress | DXR 효과 선정·AS 수명/지원/폴백 설계 2; 전환 실험/구현 제외 |
| 4.8 | `GPU-9` | 0.5 | 2 | todo | 공유 자원·선후·최소 수직 슬라이스와 실제 구현 공수 확정 2 |
| 4.85 | `RTP-0` | 미산정 | 5 | todo | 소비 경로/지원/출처 조사 2 + 모드·fixture·측정 예산/manifest 3. GPU-3/9의 교차 설계 비용은 재계상하지 않음 |
| 4.85 | `RTP-1` | 미산정 | 18 | todo | 중립 RT 타입·DX12 AS/pipeline/table 7 + scene/remap·graph 의존/수명 6 + oracle·cold/warm/실패 검증 5 |
| 4.85 | `RTP-2` | 미산정 | 20 | todo | RT hit→graph material·texture/normal/LOD 7 + BSDF evaluate/sample/PDF·light/environment/MIS 입력 8 + 의미·세대·재질 검증 5 |
| 4.85 | `RTP-3` | 미산정 | 16 | todo | accumulation/reprojection/history 5 + 기본 temporal/spatial denoising·입출력 7 + 움직임/reset/두 view 검증 4. TR 공통 motion 생산은 별도 |
| 4.85 | `RTP-4` | 미산정 | 6 | todo | directional hard shadow·Deferred 합성 3 + off-camera caster·bias·fallback 검증 3 |
| 4.85 | `RTP-5` | 미산정 | 28 | todo | 공통 효과 baseline/AS 공유 스트레스·EXP 판정 4 + 반사 5 + GI 6 + 굴절 5 + volume 경계 5 + fog visibility 3. 각 효과 prototype·oracle·채택 효과 통합 포함 |
| 4.85 | `RTP-6` | 미산정 | 12 | todo | camera/multibounce/NEE/MIS/Russian roulette 7 + 고샘플 reference·수렴/에너지 fixture 5. 재질 evaluator는 RTP-2 재사용 |
| 4.85 | `RTP-7` | 미산정 | 10 | todo | interactive 제품 graph·HDR/post/display 6 + Scene/Game/Player 기능 smoke·저샘플 예산 검증 4. denoise는 RTP-3 재사용 |
| 4.85 | `RTP-8` | 미산정 | 28 | todo | 기본 ReSTIR PT reservoir·shift/weight 10 + Enhanced 개선 구현 10 + 동일 spp/예산·편향/상관/시간축 비교 8 |
| 4.85 | `RTP-9` | 미산정 | 18 | todo | cage 생성/mesh 분할·remap·LOD 10 + cook/세대/기존 자산 보존 4 + 변형·경계 reference fixture 4 |
| 4.85 | `RTP-10` | 미산정 | 18 | todo | 기존 deformation+BLAS update/rebuild 비교 경로 4 + cage 변형/ray 변환/RT geometry route 8 + clipping/normal/t/alpha·두 모드·메모리/전체 비용 검증 6 |
| 4.85 | `RTP-11` | 미산정 | 6 | todo | 모드/프로파일·설정 저장/재로드·진단 4 + frame seal 전환·fallback 검증 2. Editor 전체 UI 재설계는 별도 |
| 4.85 | `RTP-12` | 미산정 | 18 | todo | 통합 하네스/계측 4 + D/R 세 모드·Editor/Player·view/in-flight 스트레스 6 + 실패/수명·제품 회귀 결함 대응 8. 행별 알고리즘 oracle와 중복하지 않음 |
| 4.85 | `RTP-13` | 미산정 | 5 | todo | 효과/연구 결과·채택/잔여/지원 감사 3 + 재현 패키지·공수/정본 최종 정합성 2. GPU-9 설계 완료와 별도 제품 결과 감사 |
| 4.9 | `BP-0` | 미산정 | 3 | todo | 입력/시간축 2 + 지원·실패 계약 1 |
| 4.9 | `BP-1` | 미산정 | 3 | todo | RenderDoc capture/replay·marker 대응 2 + 반복/보존 1 |
| 4.9 | `BP-2` | 미산정 | 5 | todo | geometry/상수/binding 2 + texture/depth/history 2 + 최초 차이 추적 1 |
| 4.9 | `BP-3` | 미산정 | 4 | todo | 원본 추출/규약 2 + 픽셀 통계/차영상/좌표 1 + nonfinite/실패 변이 1 |
| 4.9 | `BP-4` | 미산정 | 5 | todo | 현재 알려진 결함의 원인 수정·재캡처 예산 5; 새 대규모 결함은 별도 재산정 |
| 4.9 | `BP-5` | 미산정 | 2 | todo | 구성/장치별 반복·패키지/수용 보고 2 |

## 선행 관계

4.3은 MAT-0~8 기반에서 BASE-0→RG1→RG2→RG3~6을 진행한다. Q0는 RG8/L4가 공통 소비하고,
RG-V는 RG3부터 붙여 RG6에서 기본 제품 대응을 닫는다. 4.5는 BASE-0을 받아 RG 전체 완료를 기다리지 않는다.
4.6 CSRP-5는 RG6의 DX12 제품 전환을 소비한다. MAT-9 성능은 실제 GPU-driven/IBL 재사용 구현과
측정 뒤 회수하며 SSS/투과 품질은 별도 조건이다. GPU 설계 9일을 실제 구현 완료 비용으로 세지 않는다.
4.9는 비교할 기능의 DX12 기준선을 입력으로 받고 원 페이즈로 돌아가는 차단 edge를 만들지 않는다.
