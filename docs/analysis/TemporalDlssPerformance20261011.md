# DLSS Performance / Ultra Performance 검증

2026-10-11, fixture 계층 동기화 수정이 포함된 기존 Debug/Release 제품으로 Performance → Ultra Performance 순서의 4개 프로세스를 실행했다. 실행 파일 해시는 직전 Balanced 수정 검증과 동일함을 확인했다. 두 모드의 기본 preset을 유지했으며 이번 검증에서는 제품 코드 수정이나 재빌드가 필요하지 않았다.

## 결과

DX12 RTX 2080 Ti, display 760×312, 고정 SDK 60 Hz, 실제 RT warmup 34회 이후 기준+이동/사라짐/회복 17캡처씩 총 68캡처다. 모든 실행에서 요청한 DLSS 모드가 실제 동작했으며 GPU validation 메시지/문제/누락 0, 정상 종료 0이다. 각 모드의 Debug/Release 17프레임 × 12 attachment 해시가 동일하다. 이전 Hierarchy 단정도 이번 실행에서는 발생하지 않았다.

| 모드 | 렌더 해상도 | 첫 이동 HDR 평균/p95 | 첫 이동 display 평균/p95 | 전체 HDR 최대 평균/p95 |
|---|---|---|---|---|
| Performance | 380×156 | 0.00099291 / 0.00173060 | 0.00489366 / 0.01449275 | 0.02101970 / 0.02614379 |
| Ultra Performance | 253×104 | 0.00443685 / 0.00673336 | 0.01230674 / 0.01428571 | 0.01796734 / 0.02307249 |

기존 허용치 평균 0.10/p95 0.25를 그대로 적용했다. 여기서 Performance는 DLSS 품질 모드 이름이며 CPU/GPU 성능 개선을 측정한 결과가 아니다.

근거: `Artifacts/PR169/dlss-performance-20261011/checkpoint.json`, 각 실행의 oracle/launch/validation 기록. 이전 Quality·Native AA·Balanced 결과까지 누적하면 이 해상도와 warmed fixture 범위의 다섯 품질 모드 모두 D/R 근거가 있다. 서로 다른 시점의 제품 검증 결과이며 현재 빌드로 다섯 모드를 동시에 재검사했다는 뜻은 아니다.

## 남은 범위

Quality 이외 모드의 startup, 다른 해상도, 실제 모델/재질/얇은 형상, 가변 시간, reset/resize, 깜빡임 및 성능 수용은 남는다. TU3는 progress/기성 0, PHASE 4.5 전체는 12/71로 유지한다. 실제 FG/다른 GPU/외부 물리 지연 검증을 대체하지 않는다.

## 저장

구성·모드별 첫 이동 대표 4개만 보존했다. 비교 완료 후 실제 회수량은 삭제 시점별 여유 공간 변화 합계 1.07756 GiB, 최종 여유 85.18118 GiB다. 판정·로그·manifest·해시는 보존했으며 삭제 ledger가 전체 시퀀스 raw 재생 불가를 명시한다. 추가 비교용 원본 보류는 없다.
