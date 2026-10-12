# TR0 CPU 기록 비용 후속 — 2026-10-11

정본은 `Artifacts/PR169/tr0-rg6-cpu-20261011-v6/final-result.json`과 `cpu-improvement.json`이다. SDK 활성 Editor legacy/product × Debug/Release 전체 RG6 행렬을 재검증했다. 마지막 수용 결과 v3 대비 제품 CPU 기록 중앙값은 Debug 14.88%, Release 16.45% 감소했다. 절대 감소·legacy 대비 비율 감소·독립 실행별 감소를 모두 10% 이상 요구하는 기존 기준을 통과했다.

| 구성 | 현재 legacy | 직전 수용 제품 v3 | 현재 제품 v6 | 제품 감소 | 현재 legacy 대비 잔여 | 제품 p95 / 최대 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Debug | 1.64830ms | 2.37030ms | 2.01750ms | 14.88% | +22.40% | 2.3048 / 2.5803ms |
| Release | 1.09415ms | 1.48505ms | 1.24070ms | 16.45% | +13.39% | 1.5630 / 1.8717ms |

독립 제품 실행별 중앙값은 Debug 2.01025/2.01925ms, Release 1.23610/1.24305ms다. 마지막 수용 v3의 잔여 +46.94/+32.20%에서 줄었지만 동일 비용으로 닫지는 않는다. 모든 정상 표본을 사용했고 느린 표본을 제외하지 않았다. 수치는 GPU validation을 켠 정적 DX12 queue mode 0, 332×182 authored plane/camera/light의 native real-frame CPU graph recording 구간이다. 전체 CPU frame·GPU budget, 모든 asset/해상도/API, 실제 DLSS/FG·UI 육안·외부 계측의 수용으로 확대하지 않는다.

진단에서 Grid는 GBuffer GPU depth 결과만 읽는데 GPU edge가 CPU join으로 이어져 마지막 Grid/Gizmo/Present 구간이 별도 기록 wave로 밀렸다. 준비된 불변 camera/style/size/handle을 읽는 Grid와 Grid.DisplayDepth를 독립 기록 계약으로 연결했다. 정상 제품 recording waves/dispatches/worker jobs가 v3의 2/2/5에서 1/1/4로 줄었다. worker jobs는 호출 합계다.

그 수정만 적용한 v4는 기능 검증을 통과했지만 Release 독립 실행 1.28945/1.49895ms로 개선이 재현되지 않아 성능 완료로 수용하지 않았다. `v4/improvement.log`와 전체 프로파일·판정을 보존했다.

추가 진단의 worker 시작 중앙값은 0.041–0.053ms였고, 실제 기록 body는 가운데 target 두 개에 몰렸다. 두 번째 실행 target별 body 중앙값은 0.8264/0.9878/0.9645/0.6636ms였다. 진단 계측용 warm 요약과 cold/spike 원 로그는 별도로 보존했고, 최종 행렬에서는 계측을 제거했다. `tr0-rg6-cpu-tail-diagnostic-20261011-v2/diagnosis.json`은 수용 성능 자료가 아니다.

EnhancedRenderGraph에 opt-in CPU 기록량 분할을 추가했다. callback 기본 비용·선언된 draw-batch 기록 비용·선언된 resource usages/반복 phase 사용량을 참고해 비어 있지 않은 연속 command target 구간을 만든다. 예상 최대 기록량이 작아지고 CPU 대기 wave가 늘지 않을 때만 후보를 채택한다. 기본은 기존 분할이며 현재 제품의 ExplicitVersioned 경로만 opt-in한다. GPU 실행 순서·실제 상태 배리어·queue 소유권·기존 병렬 decline threshold를 유지한다. Temporal.DepthCopy와 SSGI.StoreHistory도 frozen handles를 읽는 GPU 복사로 감사하고 독립 기록 계약을 추가했다. compute compatibility는 추가하지 않았다.

초기 v5 후보는 실제 emitted barrier 수를 비용 힌트로 썼다. SSGI history 버퍼 교대 상태가 barrier 수를 바꾸면서 capture-11의 CPU wave가 2에서 3으로 달라져 100-capture 결정성 검사가 실패했다. 선언된 resource usages로 고쳐 물리 초기 상태에 따른 분할 변동을 제거했다. v5 실패 소스 원본은 기록된 SHA256과 일치하며 `failed-source-identity.json`으로 확인했다. 실패 원본 capture-0/capture-11 및 로그·프로파일·manifest를 보존하고 나머지 중복 raw는 정리했다.

최종 제품 정상 400개 표본 모두 balanced=true·1/1/4, legacy 400개 모두 balanced=false·1/1/4다. 최종 캡처는 기존 수용 결과와 같은 3 waves를 유지한다. Debug/Release 각각 100개 전체 캡처 및 독립 두 번째 실행의 결정성 검사를 통과했다. 전후 원본 GPU graph 차이는 generation/graphEpoch뿐이다. CPU 카운터 예외 비교기는 사용하지 않았고 기존 같은-backend full-topology 비교기를 수정 없이 사용했다.

SDK 활성 4빌드·8독립 프로세스·800 정상 CPU/GPU 표본·408 graph captures·120 population/3 cutover comparator 변이 거부를 통과했다. 모두 정상 exit 0·GPU validation 문제/누락 0·소스 변경 0이다. A/B 네 비교는 display 차이 0·전체 허용 초과 0, 제품 전후 D/R 각각 17 attachments 및 제품 D/R 17 attachments는 최대 오차 0이다. Native RG4는 기존 immediate·1/2/4 workers·split·decline·CPU joins를 유지하며, weighted contiguous GPU pixels 및 대기가 늘어나는 후보의 기존 분할 복귀도 검사한다. GPU retirement 뒤 진단 assertion을 실행한다.

남은 비용에는 제품의 추가 작업도 포함된다. 전체 정상 표본에서 GPU timestamp scope가 legacy 38개, 제품 50개이며 각각 scope 집합이 일정했다. 추가 geometry occlusion pyramid/cull, occluder depth, Temporal.DepthCopy 등이 관측된다. 이 수는 CPU callback 수가 아니며 추가 작업의 CPU 기여 시간은 따로 분해하지 않았다. `normal-gpu-workload-delta.json`에 전체 800개 표본의 집합 감사를 남겼다.

최신 성공 대표 raw는 legacy/product × D/R의 4개만 남겼다. v5에 필요한 실패 raw 두 개와 첫 실패 dump는 유지했다. 비교가 끝난 v3/v4 및 v5 성공 reference raw·중복 최소 reference 바이너리·진단 raw는 정리하고 로그/manifest/hash/판정/ceprof는 보존했다. 이전 픽셀·바이너리 replay가 닫힌 범위를 결과와 ledger에 명시했다. 최종 reference는 exe/runtime DLL만 보존하며 전체 실행 폴더/PDB를 복제하지 않았다.

이번 최종 행렬의 중복 raw 실제 회수는 구성별 1.50/1.50/0.95/0.95GiB, 마지막 정리는 0.58GiB다. 마지막 실제 디스크 여유는 87.40GiB이며 30GiB 하한을 유지했다. 다음 검증도 구성마다 비교용 두 대표만 임시 pin하고, 최종 비교 뒤 하나로 줄이는 정책을 유지한다.

TR0는 progress·기성 0, PHASE 4.5는 12/71을 유지한다. 전체 계측 표면/제품 budget/actual generated 신원/외부 기구 측정의 잔여는 기존 계획서와 checkpoint를 따른다.
