# DLSS Native AA 첫 이동 잔상

## 원인 분리와 수정

남은 DLSS 품질 모드를 검사하던 중 Native AA의 첫 이동 프레임이 기존 HDR/display 잔상 기준을 초과했다. 이후 모드 행렬을 중단하고 이 실패를 먼저 수정했다. Quality에만 지정돼 있던 preset J를 Native AA의 `dlaaPreset`에도 크기 질의/dispatch 양쪽에서 지정했다. Balanced/Performance/Ultra Performance의 기본 설정은 유지한다.

고정된 실제 RT warmup 34회에서 기존 기본 모델과 J의 17프레임 × SDK 입력 11종 raw SHA256이 모두 같았다. 현재/이전 jitter, SDK 시간, reset 및 품질 값도 동일했다. 이 입력에서 모델 선택이 결과 차이를 만든다는 근거이며, SDK 내부 구현 원인을 단정하는 것은 아니다. 폴백이나 판정 허용치 변경은 없다.

Debug 첫 이동 HDR 평균 잔상은 0.1578491 → 0.00010984, p95는 0.3663462 → 0.00019231이다. Display 평균은 0.2337381 → 0.00157984, p95는 0.4782609 → 0.01449275이다. 기준 평균 0.10/p95 0.25는 유지했다.

## 수용 범위

DX12 RTX 2080 Ti, Native AA, display/render 760×312, 고정 SDK 60 Hz, 불투명 이동/사라짐/회복 fixture에 한정한다. 최종 제품 실행 및 비교 결과는 `Artifacts/PR169/dlss-qualities-20261011/checkpoint.json`에 기록한다. 전체 PHASE 4.5 완료 또는 TU3 전체 품질 수용을 뜻하지 않는다.

Native AA startup, Balanced/Performance/Ultra Performance, 다른 해상도, 실제 모델/재질/얇은 형상, 가변 시간, reset/resize, 깜빡임/성능 검사가 남는다. J의 깜빡임 교환 조건은 이전 Quality 분석과 동일하게 남아 있다.

## 저장 관리

기존 실패 전체 1개는 원인 분석용으로 보존한다. 수정 성공 원본은 입력 비교 및 Debug/Release 비교가 끝난 뒤 구성별 첫 이동 대표만 남기며 판정/로그/manifest와 삭제 ledger는 보존한다. 전체 바이너리 사본은 만들지 않는다.

최종 Debug/Release 빌드 및 34캡처 통과. 구성 간 17프레임 × 12 attachment 해시 동일, 구성별 history 506단정 통과, GPU validation 메시지/문제/누락 0, 정상 종료 0. 비교 완료 후 성공 raw 1.44 GiB를 회수했고 여유 85.71 GiB를 확인했다. 전체 시퀀스 raw는 삭제돼 과거 전체 픽셀 재생은 불가하며 대표 frame-1과 판정/해시를 보존했다.
