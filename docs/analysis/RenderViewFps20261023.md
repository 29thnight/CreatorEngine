# Scene/Game 렌더 뷰 FPS 표시 통일 — 2026-10-23

Scene과 Game 모두 **해당 뷰의 GPU 렌더 완료 카운터 증가량 ÷ 경과 시간**을 FPS로 표시한다. 공용 `RenderViewFps` 계산기를 사용하며 표시 갱신 간격은 0.5초다. `ImGuiIO::Framerate`와 UI 프레임 수는 계산에 사용하지 않는다.

## 구현

- Scene 우측 하단 `Scene FPS`, Game 우측 하단 `Game FPS`를 공용 배지로 표시한다. Scene 전체 통계도 `Scene FPS`로 맞췄다.
- 두 뷰 모두 이미지와 함께 조회한 `displayed.frame.promotionCount`를 읽는다. Game의 별도 display snapshot 조회를 제거했다.
- 뷰 ID·장면 epoch 변경, 카운터 감소, 준비되지 않은 출력, 시간 역행 및 2초 초과 샘플 공백이면 샘플을 초기화한다.
- 같은 UI 프레임에서 Game Host와 Preview가 여러 번 그려져도 렌더 카운터를 중복 계산하지 않는다. FPS 표시 설정은 기존 Scene Show → FPS를 공유한다.

## 실제 창 검증

offscreen 옵션 없이 Debug/Release 창을 각각 전경에 두고 Play/Stop 버튼을 직접 클릭했다. 두 구성 모두 SampleScene(엔티티 3개), 창 클라이언트 2880×1665, Scene/Game 렌더 1496×692, DX12 native, 업스케일러/프레임 생성 없음이다. `--command-service`로 읽기 전용 상태를 조회했다. 각 단계의 foreground=true, 재생 확정 상태, 해상도를 검증했다.

표시는 공용 계산기의 0.5초 샘플이며 독립 측정은 약 15초 평균이다. 별도 카운터 조회로 0.5초 이상의 가까운 관측 구간 FPS 범위도 계산해, 화면에서 읽은 값이 그 범위 안에 드는지 검증했다. 평균과 순간 표시를 같은 값이라고 취급하지 않았다.

| 구성 | 상태/뷰 | 보존 화면 표시 | 약 15초 카운터 평균 | 짧은 관측 구간 범위 |
|---|---|---:|---:|---:|
| Debug | before-play / scene | 61.0 FPS | 60.0 FPS | 56.5–64.4 FPS |
| Debug | playing / game | 203.7 FPS | 192.2 FPS | 177.6–204.8 FPS |
| Debug | stopped-again / scene | 59.1 FPS | 60.1 FPS | 56.7–62.7 FPS |
| Release | before-play / scene | 60.0 FPS | 60.0 FPS | 57.0–63.3 FPS |
| Release | playing / game | 1085.5 FPS | 1019.6 FPS | 885.7–1152.5 FPS |
| Release | stopped-again / scene | 58.8 FPS | 60.0 FPS | 57.3–63.8 FPS |

모든 화면 값이 독립 카운터 관측 범위 안에 들었다. Play 진입 시 Game FPS 0.0에서 새 샘플이 시작되는 것과 Stop 후 Scene 표시 복귀도 확인했다.

## 빌드와 계산 검사

Debug 빌드 경고 1건·오류 0건, Release 빌드 경고 9건·오류 0건. 검사한 실행 파일과 빌드 해시가 일치한다. 공용 계산기 검사에서는 240번/초의 UI 조회와 별개로 Scene 60 FPS·Game 960 FPS가 계산되는지, 같은 시각 중복 조회, 준비 해제/재개, epoch 교체, 카운터 감소, 장시간 미표시 후 재개를 검증했다.

프로젝트 EngineSettings와 사용자 active.workspace는 측정 전 원본과 바이트 단위로 동일하다. 기존 로컬 변경은 보존했다.

## 원본 증거

`Artifacts/frame-regression-20261023/` 아래에 다음을 보존했다.

- `render-view-fps-build/`: Debug/Release 빌드 로그와 바이너리 SHA-256.
- `DebugRenderFps/`, `ReleaseRenderFps/`: 단계별 원본 카운터·상태·평균 및 구간 범위 JSON.
- `render-fps-evidence/`: 실제 창의 Play 전·중·Stop 후 JPEG 6장.
- `render-fps-acceptance.json`: 화면에서 읽은 수치와 카운터 대조 결과.
- `render-fps-source.json`: 구현 파일 해시.
- `render-view-fps-counter.cpp/.exe`: 공용 계산기 검사.
