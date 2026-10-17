# TR0 — 측정 표면과 기존 소비자 후속

## 판정 범위

2026-10-11 구현 후 SDK 활성 Debug/Release Editor·ProfilerViewer 빌드와 검증을 통과했다. 최종 판정 정본은
`Artifacts/PR169/tr0-surfaces-sdk-20261011/tr0-surfaces-checkpoint-20261011.json`이다.
검증 수치는 최종 정본과 현재 runtime hash를 대조했다. TR0는 progress·기성 0이며
PHASE 4.5 기성 12/71을 유지한다.

## 구현

- Scene 통계의 GPU 측정은 실제 실프레임·view·publication·해상도 증거를 검사한다.
  표시 크기가 바뀌거나 신원이 누락된 기록은 사용할 수 없다. 250ms UI 캐시가
  현재 이미지보다 이전이면 실프레임 번호·렌더/표시 크기·publication 나이를 함께
  표시한다. 이후 사용자 설정으로 과거 GPU 측정을 재표기하지 않는다.
- ProfilerViewer의 GPU 비용·pass 목록은 공용 `matches_gpu_measurement`를 사용한다.
  선택한 완료 view/frame/display extent와 실제 GPU 증거를 연결하고 generated·unknown,
  누락 신원·불일치 해상도·잘못된 재구성 상태를 거부한다. 이전 codec을 변경하지
  않았으며 PLRD schema 3과 CEPROF wire 형식은 유지한다.
- 애니메이션·IK·재질 구성 간 비교·Editor 환경 가시성의 네 픽셀 소비자가
  `audit_pixel_measurement`를 먼저 실행한다. 캡처·측정·attachment의 시간축 증거,
  강제 native TU/FG/NIS/DeepDVC 제외와 render/display extent가 일치해야 한다.
  attachment 일부만 읽는 검사도 이 계약을 건너뛸 수 없다.
- BASE-0 성능 수집은 측정 전에 native 설정을 요청하고 실제 provenance를 검사한다.
  저장 행에 CPU/GPU 증거·측정 GPU 신원·표시 크기·각 비용 축을 남긴다. RG6 소비자는
  저장된 행을 다시 검사한다. `Native record CPU`는 native graph recording CPU이며
  전체 frame time이 아니다. GPU pass 합계는 겹침을 포함할 수 있어 wall time이 아니다.
  두 비동기 측정 창을 동일 프레임의 짝으로 주장하지 않는다.
- native 소비자 드라이버에도 디스크 30GiB 최소 여유와 명령 결과 수 검사를 연결했다.
  golden 검사의 `-RequireActiveDlss`는 요청 상태에 더해 캡처 전 실제 DLSS 성공을 요구한다.

## 검증 경계

두 구성 모두 캡처 전 실제 DLSS 활성·업스케일 성공을 확인했다. 강제 native 전환 후
1498×730 반복 golden 캡처의 모든 attachment 최대 오차는 0이며 29변이를 거부했다.
DX12 validation 문제·메시지·누락은 0, native profiler 기록은 complete·loss 0이고
모든 GPU event가 연결됐다. 각 구성 C++ 세 검사가 통과했다.

각 구성에서 네 실제 픽셀 소비자 108거부, BASE-0/RG6 저장 행 46거부,
native preflight 62거부, 공용 timing 52거부, wire 12변이, 분석 9변이,
예산 13거부와 초과 예산 대조를 통과했다. reader 10·외부 계약 27·저장 관리 5검사도
통과했다. 외부 계약 검사는 실제 외부 기구 계측 수용을 의미하지 않는다.

`verify-tr0-diagnostics.ps1`는 Debug/Release에서 assert를 유지한 채 diagnostics codec과
GPU/display 연결, temporal provenance와 scoped exclusion, profiler codec/recording 소유권의
세 독립 C++ 검사를 실행한다. 픽셀 소비자 검사는 실제 manifest의 27변이를 네 실제
entry point에 전달하며 픽셀 파일을 복제하지 않는다.

BASE-0/RG6 저장 행 검사는 실제 API snapshot의 신원을 사용하지만 CPU duration은
명시적 테스트 값 0이다. 이 검사는 metadata admission만 증명하며 전체 fixture의
성능 수집·반복 population이나 CPU 성능 판정을 증명하지 않는다.

이번 범위를 전체 provider/품질/해상도 행렬, UI 육안·스크린샷 수용, 실제 Player generated
출력, 외부 물리 표시/지연, master 비교 또는 승인된 제품 예산으로 확대하지 않는다.
TR0 전체 종료를 이 후속의 로컬 성공으로 대체하지 않는다. Vulkan 비교는 PHASE 4.9 소유다.

## 용량 관리

기존 Bin/Build 출력과 원본 fixture를 재사용하며 전체 바이너리·프로젝트 사본은 만들지
않는다. native 빌드의 중간 성공 기록은 최종 SDK 활성 검증 이후 원본 픽셀을 제거한다.
SDK 최종 구성별 첫 golden 캡처만 보존하고 두 번째 캡처의 로그·판정·manifest는 유지한다.
전체 `.ceprof`·로그·변이 결과와 필요한 실패 로그를 보존하며, 제거된 픽셀은 재생 불가임을
retention ledger에 기록한다. 회수량은 실제 디스크 free-space 변화로 측정한다.

판정 완료 후 중간 native 원본 44개와 SDK 반복 원본 22개를 제거했다.
실제 회수량은 약 1.00GiB이며 정리 직후 C: 여유는 93.54GiB다.
SDK Debug/Release 대표 캡처 두 개와 모든 판정·로그·프로파일 및 최초 실패 로그는
보존했다. 전체 바이너리·fixture 사본은 0개다. 이번 빌드는 기존 출력 경로를 재사용했지만
SDK 구성 변경으로 모듈을 재컴파일했으므로 빌드 출력 증가는 별도이며 아티팩트 회수량과
혼동하지 않는다. 실제 시작/종료 여유는 정본의 storage에 남겼다.


2026-10-11 후속: 현재 바이너리의 BASE-0 실제 population 및 용량 정리는 [TemporalBase0ConsumerWorkload20261011](TemporalBase0ConsumerWorkload20261011.md)과 tr0-base0-workload-20261011/checkpoint.json이 정본이다. SDK D/R 빌드·400표본·204캡처 수용, raw 대표 성공 2/필요 실패 2 및 로그 보존, 여유 93.21GiB. 이전 runtime 해시의 검증은 당시 실행 증거로 유지한다. TR0 progress·기성 0/PHASE 4.5 12/71 유지.
