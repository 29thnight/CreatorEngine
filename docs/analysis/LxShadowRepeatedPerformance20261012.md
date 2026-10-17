# LX shadow repeated performance — 2026-10-12

기존 목록 7번의 선택한 합성 범위 측정을 완료했다. GPU 계측 용량을 1024에서 4096 패스/프레임으로 늘려 MatGrid 8×8의 1710개 구간을 누락 없이 수집했다. 현재 Release 바이너리로 3개 장면, 경로별 4회씩 총 24개 정상 기록을 확보했다. **측정 산출물은 통과했지만 성능 이득은 수용하지 않는다.** 세 장면 모두 그림자 평균합 비교의 독립 반복 CV 기준을 통과하지 못했다.

## 변경과 방법

- `EnhancedSceneRendererLiveDX12Adapter.cpp`의 그래픽/compute 초기화 용량을 공통 상수 4096으로 통일했다. 3프레임에서 24,576개 timestamp query, 192 KiB readback을 예약한다. 메타데이터 메모리는 별도이며, 이 상한을 넘으면 여전히 계측 실패다. 무제한 용량이나 동적 증가를 구현하지 않았다.
- `measure-lx-shadow-meshlets.ps1`은 C0/C1/C2가 모두 계측됐는지 검사하고 호출당 시간, CPU 준비·기록 비용, 시작/종료 카운터, 소스·바이너리 해시와 정상 종료를 기록한다. 상속된 Debug query 제한은 제거한다.
- `measure-lx-shadow-repeat.ps1`은 indexed→meshlets→meshlets→indexed를 두 번 실행한다. 독립 프로세스·격리 프로젝트, 경로별 4개의 8초 녹화다. 측정 중 다른 빌드나 GPU 검증을 실행하지 않았다.
- `assess-lx-shadow-performance.py`는 프로파일 SHA, 출처, 동일 모델·셰이더·배치·카메라 입력, 동일 바이너리와 해상도, 실뷰당 3개 캐스케이드 누락 없음, 정상 종료를 확인한다. 동일 캡처의 반복 제출은 의도적으로 거부했다.
- 변동계수는 4개의 독립 녹화 평균에 대한 sample SD / mean이다. 두 경로 모두 CV ≤ 10%여야 안정성 기준 통과다. 표의 변화율은 `(meshlet/indexed-1)*100`이며 음수는 비용 감소다. 불안정한 평균의 방향을 확정 이득/손실로 수용하지 않는다.

Release runtime SHA256: `5E9DF26F5B0BF237B8E5D0C535277A03003070C62A7CB241D90E1453ACA10A0F`.

실제 뷰와 display extent는 모두 **332×182**였다. 시간 측정은 validation off, 기능 회귀는 별도의 GPU validation on 실행으로 분리했다. `CREATOR_LX_MESHLETS`는 그림자 외 표면 경로에도 영향을 주므로 CPU 값은 전체 장면 준비/기록 비용이며 그림자만의 인과 비용으로 분리한 값이 아니다. 제품 해상도, FPS, 표시 지연, compute 사전 컬링 성능 수용은 아니다.

## GPU 비용

캐스케이드 호출당 평균을 사용한다. 각 녹화에서 C0/C1/C2 호출 수는 실제 뷰 렌더 수와 같았다. `Shadow.SumOfPassMeans`는 세 평균의 합이며, 엔진/UI 프레임당 값과 혼동하지 않는다. 캐스케이드별 원시 통계와 엔진 프레임당 값도 개별 `result.json`과 `.ceprof.summary.json`에 보존했다.

| 장면 | GPU 항목 | indexed ms | meshlet ms | indexed CV % | meshlet CV % | 변화 % | CV ≤ 10% |
|---|---|---:|---:|---:|---:|---:|---|
| MatGrid 8×8 | LX.Scene.Shadow.C0 | 5.3281 | 5.2745 | 10.34 | 1.71 | -1.01 | 미수용 |
| MatGrid 8×8 | LX.Scene.Shadow.C1 | 4.0816 | 4.9844 | 15.66 | 3.98 | 22.12 | 미수용 |
| MatGrid 8×8 | LX.Scene.Shadow.C2 | 3.1977 | 3.3163 | 10.06 | 7.59 | 3.71 | 미수용 |
| MatGrid 8×8 | Shadow.SumOfPassMeans | 12.6074 | 13.5753 | 11.16 | 1.28 | 7.68 | 미수용 |
| Suzanne 12×12, far=30 | LX.Scene.Shadow.C0 | 0.1573 | 0.1919 | 13.48 | 4.93 | 22.02 | 미수용 |
| Suzanne 12×12, far=30 | LX.Scene.Shadow.C1 | 0.4412 | 0.5074 | 26.54 | 15.52 | 15.00 | 미수용 |
| Suzanne 12×12, far=30 | LX.Scene.Shadow.C2 | 0.3952 | 0.6514 | 11.25 | 19.00 | 64.82 | 미수용 |
| Suzanne 12×12, far=30 | Shadow.SumOfPassMeans | 0.9937 | 1.3507 | 15.82 | 14.67 | 35.93 | 미수용 |
| Suzanne 12×12 | LX.Scene.Shadow.C0 | 0.5799 | 0.5330 | 6.21 | 21.10 | -8.09 | 미수용 |
| Suzanne 12×12 | LX.Scene.Shadow.C1 | 0.3860 | 0.4124 | 3.28 | 14.50 | 6.85 | 미수용 |
| Suzanne 12×12 | LX.Scene.Shadow.C2 | 0.4617 | 0.3560 | 12.01 | 21.36 | -22.90 | 미수용 |
| Suzanne 12×12 | Shadow.SumOfPassMeans | 1.4276 | 1.3014 | 6.56 | 18.61 | -8.84 | 미수용 |

MatGrid는 각 캡처·캐스케이드당 24개 실제 렌더 샘플을 포함한다. Suzanne 기본은 439~508개, far=30 조건은 446~507개다. 따라서 MatGrid는 다수 GPU 구간을 검증한 스트레스 장면이지만 실제 렌더 표본은 적다는 한계가 있다.

## CPU 비용

준비와 기록은 각 marker 호출당 평균이다. `RenderThreadFrame`은 다른 단계와 대기를 포함하므로 GPU 시간에 더해 FPS나 지연으로 계산하지 않는다.

| 장면 | CPU 항목 | indexed ms | meshlet ms | indexed CV % | meshlet CV % | 변화 % | CV ≤ 10% |
|---|---|---:|---:|---:|---:|---:|---|
| MatGrid 8×8 | CPU.RenderThreadFrame | 158.4106 | 157.1860 | 2.44 | 0.73 | -0.77 | 통과 |
| MatGrid 8×8 | CPU.MaterialGraphScenePrepare | 192.5034 | 191.7006 | 0.86 | 0.58 | -0.42 | 통과 |
| MatGrid 8×8 | CPU.RenderCommandRecord | 4.7272 | 5.0307 | 2.95 | 0.48 | 6.42 | 통과 |
| Suzanne 12×12, far=30 | CPU.RenderThreadFrame | 10.5477 | 10.7631 | 10.93 | 6.40 | 2.04 | 미수용 |
| Suzanne 12×12, far=30 | CPU.MaterialGraphScenePrepare | 5.6543 | 5.7752 | 10.78 | 6.88 | 2.14 | 미수용 |
| Suzanne 12×12, far=30 | CPU.RenderCommandRecord | 1.1560 | 1.2097 | 6.13 | 3.18 | 4.65 | 통과 |
| Suzanne 12×12 | CPU.RenderThreadFrame | 11.0278 | 10.4416 | 4.30 | 11.42 | -5.32 | 미수용 |
| Suzanne 12×12 | CPU.MaterialGraphScenePrepare | 5.9916 | 5.5676 | 3.10 | 13.01 | -7.08 | 미수용 |
| Suzanne 12×12 | CPU.RenderCommandRecord | 1.1897 | 1.2101 | 1.46 | 5.77 | 1.72 | 통과 |

CPU 기록 비용은 MatGrid 평균 +6.42%, Suzanne 기본 +1.72%, far=30 조건 +4.65%이며 해당 반복 CV 기준은 통과했다. MatGrid 준비는 약 192 ms로 큰 비용을 보인다. 이 측정만으로 특정 함수/업로드가 원인이라고 확정하지 않는다.

## 경계와 거부율

audit은 별도 실행의 **CPU 재현**이며 시간 측정에서 제외했다. 실제 GPU 생존 목록 readback 증거가 아니다.

| 장면 | C0 | C1 | C2 |
|---|---|---|---|
| MatGrid 8×8 | 0 / 16,065 (0%) | 0 / 16,065 (0%) | 0 / 16,065 (0%) |
| Suzanne 12×12 | 0 / 2,593 (0%) | 0 / 2,593 (0%) | 0 / 2,593 (0%) |
| Suzanne 12×12, far=30 | 41 / 235 (17.45%) | 150 / 1,387 (10.81%) | 0 / 2,593 (0%) |

기본 장면을 경계 성능 증거로 승격하지 않았다. `camera.lens.probe ProbeCamera 60 0.1 30`과 editor camera match로 별도 조건을 만들었다. far=30에서는 인스턴스 가시 수가 14/78/145이며 일부 인스턴스 안의 메시렛이 클립 밖으로 나간다. C0/C1의 양수 거부율과 부분 인스턴스 가시 수를 종료 조건으로 검사했다. 이는 모든 경계·재질·애니메이션 장면 수용은 아니다.

## 제외한 실패와 보관

1. MatGrid 16×16 탐색은 aggregate chunk/CPU/GPU payload 제한을 넘었다. 측정 전에 제외하고 로그와 review를 보존했다.
2. 수정 전 1024 슬롯의 MatGrid 8×8은 overflow 카운터가 13,178→30,328로 증가해 거부했다. 해당 실패와 이전 바이너리의 Suzanne 8회 기록은 보존하되 새 평균에 섞지 않았다.
3. 수정 후 MatGrid 마지막 indexed 캡처는 `complete=false`였다. 슬롯 초과/손실 카운터는 0이지만 녹화 종료 262.28 ms 시점에 GPU 제출 하나가 남아 stop drain deadline을 넘었다. 원본 `.ceprof`와 판정을 보존하고 제외했다. 원인을 검토한 뒤 **동일 바이너리·동일 절차·동일 기준**으로 한 번 재실행했고 complete=true, pending GPU=0으로 통과했다. 250 ms stop drain 경계 자체를 수정하거나 실패를 정상화하지 않았다.
4. 추가 반복 도구에서 빈 배열 초기화가 null로 열거되어 dictionary merge 오류가 났다. 명시적 배열로 수정했다. 이미 통과한 두 정상 캡처를 검증해 목록을 복구했고 이전 manifest와 복구 기록을 보존했다. GPU 결과를 재작성하지 않았다.

모든 `.ceprof`, 로그, 원시 평균 표본, SHA, manifest와 판정을 보존했다. 매 GPU 실행 전 실제 여유 공간 30 GiB 이상을 확인했다. 이 측정은 원본 그림자 픽셀을 캡처하지 않았고 픽셀 동치 판정을 새로 부여하지 않는다. 이전 12개 합성 정확도 검증의 Debug/Release 원본 144개씩은 보존했다.

## 수정 빌드의 기능 회귀

Debug/Release 빌드 성공. 각 구성의 품질·스키닝·IBL·rendergraph·scene 5개 기능 회귀, GPU validation 문제 0·dropped message 0, exit 0·quit 성공, 실행 전후 소스/binary 동일을 확인했다. Debug runtime SHA256: `AE995ED17CF88E3E2CD21D7B0B7701B800E443591E8C7EEB161454BB076624C2`. Release runtime SHA256는 위 24회 측정과 같다. [Debug 판정](../../Build/Verification/ShadowPerformance-20261012-Final/Regression-Debug/verdict.json), [Release 판정](../../Build/Verification/ShadowPerformance-20261012-Final/Regression-Release/verdict.json).

12개 장면의 전체 동치/누락 변이 매트릭스는 이번 계측 변경 뒤 다시 실행하지 않았다. 기존 [장면 검증 보고서](LxShadowCases20261012.md)의 binary SHA와 당시 판정으로 보존한다. 새로운 표면/그림자 shader 또는 컬링 알고리즘 변경 시 다시 실행해야 한다.

## 증거와 다음 판단

- [통합 판정·입력·전체 반복값](../../Build/Verification/ShadowPerformance-20261012-Final/overall.json)
- [24회 manifest](../../Build/Verification/ShadowPerformance-20261012-Final/runs.json)
- [정상 캡처 재사용 거부](../../Build/Verification/ShadowPerformance-20261012-Final/negative-verdict.json)
- [수정 전 실패 검토](../../Build/Verification/ShadowPerformance-20261012/review.json)
- [수집 불완전 캡처 검토](../../Build/Obj/LxShadowP0/Run-Release-indexed-20261012-cap4096-boundary-7/review.json)
- [반복 목록 복구 기록](../../Build/Verification/ShadowPerformance-20261012-cap4096-clip/controller-repair.json)

7번의 선택한 합성 범위 산출물은 완료했다. 8번 compute 구현의 착수 근거는 충족하지 못했다. 제품을 대표하는 입력/해상도와 안정적인 비용 비교, CPU 준비·기록 병목을 확인한 뒤 진행 판단을 갱신한다. CPU audit은 GPU 후보/생존 목록 증명이 아니며, compute 경로를 구현하면 목록 비교·픽셀 동치·누락 변이와 성능을 다시 검사해야 한다. Vulkan은 계속 PHASE 4.9 소유다.