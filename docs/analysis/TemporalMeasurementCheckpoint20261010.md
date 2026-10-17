# TR0 — golden 강제 제외와 실제 profiler wire 검증 (2026-10-10)

## 판정

TR0의 golden·profiler 계약 검증을 보완했다. TR0는 progress·기성 0을 유지하며 PHASE 4.5의 완료 집계는 TR1/TU5의 12/71(17%) 그대로다. 이번 결과를 모든 Player/성능 예산/계측 표면 또는 실제 FG 출력 신원의 전수 수용으로 확대하지 않는다.

엔진 바이너리는 이전 종료 체크포인트와 같고 재빌드하지 않았다. Debug runtime SHA256은 `35D9EBEE53C1942D7734F86DB6702920382CA657154B5A57673C3AA78FF66C5A`, Release는 `F71C118675983BE5809B959FEE7F89BF7F17D3F25F020CBE22CB9B9DC3E0788D`다. 실행 정본은 `Artifacts/PR169/tr0-measurement-checkpoint.json`이다.

## 실제 golden 캡처

새 `verify-temporal-golden-exclusion.ps1`은 독립 commandlet 장면에 실제 Cone 모델·카메라·광원을 만들고 두 차례 controlled GPU 캡처를 실행한다. 시작 환경에서 DLSS 업스케일, FSR FG, NIS sharpen, DeepDVC를 요청한다. Editor FG는 구조적으로 금지되므로 이는 FG가 실제 생성한 화면의 검사가 아니다.

각 캡처의 manifest·attachment·GPU timing에 real-frame 신원, 양쪽 해상도, native 강제 제외, upscaler/FG/NIS/DeepDVC OFF가 일치해야 한다. 실제 before/after snapshot 3개에서 요청 의도 보존과 native exclusion 해제를 검사한다. 기존 BASE-0 full manifest/graph/attachment reader와 픽셀 comparator를 사용해 실제 GPU 입력과 출력의 반복성도 검사한다.

두 구성 모두 모든 attachment/진단 stage의 비교 최대 오차 0, basic DX12 validation 메시지·문제·누락 0을 수용했다. golden metadata·해상도 누락·generated ordinal/종류·재구성/FG/공간 효과·그래프·GPU timing 및 실제 display 바이트 손상을 포함한 29개 변이를 거부한다. 정상 픽셀 반복성은 master와의 동등성 증명이 아니다.

Debug 첫 변이 실행은 검사기의 `display capture requires produced version` 가정 때문에 실패했다. 현재 `PostChain.Output`은 Create가 생산하는 정상 logical version 0이며 unproduced/imported를 뜻하지 않는다. `base0_artifacts.py`는 version > 0이면 이전 버전, version 0이면 존재하지 않는 versionCount를 넣어 실제 그래프 판정에서 거부되는지 검사하도록 고쳤다. 원래 실패 로그는 그대로 보존하고 같은 원본 캡처를 재감사했다. Release는 수정된 driver로 처음부터 실행했다.

## 실제 profiler 바이트 변이

새 `test-render-measurement-wire.py`는 이전의 최종 complete/lossless native v7 `.ceprof` 두 개를 원본으로 사용한다. 원본을 변경하지 않고 임시 복사본의 GPU measurement 바이트를 바꾼 뒤 payload/header CRC를 모두 재계산한다. 따라서 해상도·프레임 종류 계약을 깨뜨린 입력이 단순 체크섬 오류로 거부되는 결과를 통과로 세지 않는다.

구성별 12변이: render/display width/height 누락, generated ordinal을 real로 표기, 유효한 generated GPU sample, real/publication ID 누락, 다른 engine frame, 공간 효과 provenance 누락, sharpen 또는 DeepDVC 효과 혼입. 모든 변이는 wire reader 또는 native-real gate에서 거부됐고 원본 hash가 유지됐다. 유효한 generated sample은 parser가 읽을 수 있어도 native 성능 대조군에 들어갈 수 없다.

`TemporalPerformanceEvidenceTests.py`의 10개 reader/codec/admission 회귀도 통과했다. 여기에는 synthetic presenter wire round-trip·legacy·CRC/finalization/중복/생성 프레임 분리 계약이 포함된다. synthetic presenter 값은 실제 FG 표시나 물리 pacing 증거가 아니다. 이전 9변이 분석 단계 검사는 별도로 유지한다.

## 남은 종료 기준

1. 현재 사용되는 golden/timing/profiler/성능 예산/Player 계측 소비자 목록을 고정하고, 각 소비자가 누락 provenance와 실/생성 혼합을 거부하는 실행 증거를 연결한다. 이번 두 표면만으로 '모든 측정 표면' 완료를 선언하지 않는다.
2. Player actual real/generated 출력 신원과 presenter 기록의 관련 계약을 실행 수용한다. CPU Present 반환·aggregate interpolated count만으로 개별 generated 출력 신원이나 물리 표시 시각을 만들지 않는다.
3. 지원하는 계측 입력을 전수 검사한 후 TR0 3인일 종료를 판정한다. SDK 화질·실제 FG pacing/latency·master 동조건 성능 비교는 각 후속 행의 수용 범위를 유지한다.
