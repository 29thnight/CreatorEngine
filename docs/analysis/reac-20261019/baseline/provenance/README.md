# CreatorRobot Debug 재측정 10 · 2026-10-19

[측정 보고서](report.html) · [RenderGraphDeclare 내부 분석](declare-analysis/index.html)

- 측정 HEAD: `946ef4fa9b79f4e74ad09a03d79aa117277ded87`.
- 로컬 최적화는 제외했다. 원격 소스의 정의 헤더 누락을 `MaterialGraphShaderMeta.h` include 한 줄로 보정했다. `measured-source.patch`에 남겼다.
- CreatorRobot Debug 플레이, 워커 8개, Editor 전경, Scene+Game 1186×514, 워밍업 5초, 기록 ON/OFF 각 15초. ETW ON 수집과 ETW OFF 반복 수집을 구분한다.
- Game CPU 중앙값 11.687ms, 반복 11.706ms. Debug 120프레임 CPU 예산 8.333ms 미달. 완료 뷰/초는 화면 FPS가 아니다.
- 비교 기준은 9차 역사 수집이며 로컬 변경을 포함한다. 차이를 PR만의 인과 효과로 단정하지 않는다.
- Debug 빌드, DX12 Scene/Volume 회귀, 플레이 정상 종료, 렌더 출력 6종 일치, ETW 이벤트/버퍼 손실 0을 확인했다. 정확성 probe는 DebugLayer ON, GPUValidation OFF였다.

## 자료 범위

수치·검증·출처와 전체 선언 스택 집계 JSON을 게시한다. `provenance.json`은 게시 파일과 로컬 원본의 SHA256을 보관한다. `after/identity.json`과 `repeat/identity.json`은 측정 바이너리의 해시를 보관한다.

전체 ETL·ceprof·125MB 타임라인 및 바이너리/PDB는 Git 제외 대상인 `Artifacts/remeasure-10-20261019`에 남아 있다. 이 게시본만으로 전체 원본 타임라인을 재구성할 수는 없다.

`declare-analysis/scopes.py`와 `export.ps1`은 저장소 루트에서 실행했던 분석 절차이며 로컬 Artifacts 원본이 필요하다. 스코프 분해는 Game 396뷰, ETW 표본 스택은 Scene+Game을 포함한 렌더 TID 26056의 DeclareAll 호출 경로다. Inclusive 비중을 합산하거나 per-frame 절감량으로 환산하지 않는다.
