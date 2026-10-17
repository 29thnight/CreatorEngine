# DLSS 최종 바이너리 검증 공백 해소

2026-10-11. 근거: `Artifacts/PR169/dlss-final-build-20261011/checkpoint.json`, `plan.json`, `storage.json`.

직전 검증의 Quality/Native AA/Balanced 전환 결과는 Performance/Ultra Performance preset 변경 전 빌드였고, Native AA startup은 수동 노출 입력 연결 전 기록이었다. 관련 소스 해시가 직전 최종 소스 기록과 일치함을 확인하고 현재 SDK 활성 Debug/Release 바이너리에서 해당 항목을 실행했다. 이번에는 제품 코드 수정이나 재빌드가 필요하지 않았다.

- Quality/Native AA/Balanced 품질 전환·960×640/1280×720 host resize: D/R 6실행, 72캡처, 첫/여덟 번째 승인 reset 36비교 통과. depth/baseColor/SSGI/HDR/display 차이 모두 0.
- 각 전환 실행의 history 계약 506단정, GPU validation 메시지/문제/누락 0, 정상 종료 0.
- Native AA startup: D/R 2실행·34캡처 통과. 첫 관측 non-reset 프레임부터 검사.
- 신규 106캡처의 비교 대상 D/R 픽셀 해시 일치. 세 품질의 이전 전환 출력과도 동일하다.
- 기존 Performance/Ultra Performance 전환 및 나머지 네 품질 startup의 실행 바이너리 SHA256이 이번 바이너리와 동일함을 재확인했다. 따라서 다섯 품질의 지정 전환·startup 근거가 구성별 현재 바이너리로 통일됐다. 전체 항목을 이번에 모두 재실행한 것은 아니다.

성공 원본은 실행별 대표만 남겼고 대체된 이전 성공 6개 대표의 raw도 제거했다. 이번 정리의 실제 회수량은 3.576GiB, 정리 직후 여유는 82.69GiB다. ledger·로그·판정·manifest·해시는 유지한다. 제거한 raw의 과거 픽셀 재생은 불가능하며 필요한 원래 실패 기록은 보존했다.

이 결과는 DX12 RTX 2080 Ti, Editor Game 뷰, 정적 cube/floor, 고정 SDK 시간 및 지정 품질 전환/해상도에 한정한다. TR2/TR3/TU3 progress와 PHASE 4.5 기성 12/71은 유지한다. 다음 좁은 검증은 수직 FOV/투영 변경의 실제 제출 및 reset 복구다. 멀티뷰·실제 자산 화질·가변 시간·성능·FG·외부 지연 수용도 남는다.
