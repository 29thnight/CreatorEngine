> 현재 제품 FPS 표시는 Scene/Game 렌더 뷰 완료 기준으로 통일했다. 최종 구현 및 실제 창 검증은 [RenderViewFps20261023.md](RenderViewFps20261023.md)를 따른다. 아래 UI FPS 비교는 지표 혼선을 추적한 과거 기록이다.

# 실제 에디터 창 재확인 및 FPS 보고 정정 — 2026-10-23

이전 답변의 Play 중 1120.4 FPS를 에디터 화면 FPS로 제시한 것은 잘못된 보고였다. 그 값은 `promotionCount`가 6089에서 17158로 증가한 11069회를 9.8793331초로 나눈 Game 렌더 완료 처리량이다. GPU 완료 처리에서 표시 가능한 텍스처를 교체할 때 증가하며, UI가 그 텍스처로 창을 그린 횟수를 세지 않는다. 실제 에디터 창 FPS 수용값에서 제외한다.

이번에는 `--smoke-offscreen` 없이 실제 Debug/Release 창을 전경에 활성화하고 화면의 Play/Stop 버튼을 직접 눌렀다. `--command-service`는 상태 조회에 사용했다. 두 구성 모두 SampleScene(엔티티 3개, Main Camera·Directional Light), 호스트 클라이언트 2880×1665, Scene/Game 렌더 1496×692, 동일 workspace 및 프로젝트 설정이다. 각 상태에서 foreground=true와 재생 확정 상태를 확인했다.

Scene 화면에서 직접 확인한 표시: Debug Play 전 59.9 FPS·Stop 후 60.0 FPS, Release Play 전 60.0 FPS·Stop 후 약 60 FPS. Game 화면에는 FPS 오버레이가 구현되어 있지 않았다. 따라서 Game 화면에서 1120.4라는 FPS를 직접 확인한 적이 없다.

에디터 UI 갱신은 `EditorRenderer::EndRender`가 프레임당 한 번 호출하는 `publish_viewport_demand`의 `publishedFrames` 증가량/실측 시간으로 측정했다. 이것은 창 UI 프레임의 갱신 속도다. 아래 표는 모든 상태에서 같은 UI 프레임 카운터를 쓴다.

| 구성 | Play 전 UI FPS | Play 중 UI FPS | Stop 후 UI FPS |
|---|---:|---:|---:|
| Debug | 119.3 | 113.0 | 120.1 |
| Release | 120.0 | 255.4 | 120.0 |

같은 실제 창의 Play 중 Game 렌더 완료 처리량은 Debug 181.5 FPS, Release 1089.4 FPS였다. UI 프레임 속도와 렌더 완료 속도가 서로 다르게 실제 실행되므로 두 값을 같은 FPS로 제시하지 않는다. 이번 시작 장면에서 Play 중 UI 50 FPS는 재현되지 않았다. 모니터 스캔아웃 횟수는 위 UI 카운터의 정의에 포함되지 않는다.

원본 상태/샘플은 `Artifacts/frame-regression-20261023/{DebugVisible,ReleaseVisible}/{before-play,playing,stopped-again}/`와 `visible-results.json`에 보존했다. `profile.stats.engineFrame`은 프로파일러 정지 상태에서 증가하지 않았으므로 해당 조회값으로 엔진 FPS를 계산하지 않았다.

## 이전 자동화 측정 기록 — 렌더 완료 처리량으로만 해석

아래의 이전 Play 수치는 UI/화면 FPS 수용값으로 쓰지 않는다. 스케줄러 수정 및 빌드·회귀 결과는 유지한다.
# Live View 스케줄러 및 FPS 표기 수정 — 2026-10-23

## 원인과 변경

기존 Scene 스케줄러는 native 제출 완료 시각부터 1/60초를 기다렸다. 호스트가 60 FPS이면 다음 틱에서 제출 작업 시간만큼 간격이 부족해 한 틱을 건너뛰었다. 기존 헤더를 고정한 재현에서 제출 작업 0.18·1·8ms 모두 Scene 30 FPS를 만들었다.

`Plan::scheduledAt`으로 갱신 판단 시각을 보존하고, 성공한 제출만 그 시각으로 확정한다. 고정 주기의 다음 마감 시각을 유지하며 호스트 틱 오차는 주기의 1/16, 최대 1ms까지 허용한다. 연속 갱신 간 최소 간격으로 밀린 프레임의 연속 제출을 막는다. 제출 실패·강제 캡처·카메라 및 history 변경·뷰 우선순위와 GPU credit 제한은 기존 계약을 유지한다.

Scene 통계는 `Viewport FPS`, 간단 표시는 `Scene FPS`, Show 메뉴는 `FPS`로 표시한다. 기존 GPU 완료 후 display promotion 카운터를 그대로 사용한다. `dx12.live status`에도 대상별 `promotionCount`를 노출해 Scene·Game 합산을 방지했다.

## 동일 조건 재측정

실제 Editor 실행 파일을 `--development-project Dynamic_CPP --smoke-offscreen --command-service`로 실행했다. 두 구성 모두 시작 SampleScene(엔티티 3개), 창 1920×1080, 렌더/표시 1186×514, DX12, native 해상도, 업스케일러 및 프레임 생성 없음이다. 편집 상태는 기존 display 제한, Play는 기존 자동 전환에 따른 unlimited 정책이다. 시작 상태는 10초, Play/Stop 전환은 5초 예열 후 각 상태에서 약 10초 동안 프로파일 기록 없이 promotion 증가량/실측 시간을 구했다. 전체 프레임 CEPROF는 별도 구간에서 기록했다.

| 구성 | 수정 전 Scene | 수정 후 Play 전 Scene | Play 중 Game | Stop 후 Scene |
|---|---:|---:|---:|---:|
| Debug | 29.9 FPS | 59.8 FPS | 236.1 FPS | 59.8 FPS |
| Release | 29.8 FPS | 60.0 FPS | 1120.4 FPS | 60.0 FPS |

위 FPS는 완료되어 뷰에 반영된 렌더 프레임 수다. 모니터의 표시 횟수와 별도로 집계한다. Play 중 동반 Scene의 완료 FPS는 Debug 32.9, Release 58.8이었다. Scene과 Game의 카운터를 합산하지 않았다. 수정 전 Play 측정의 합산 카운터는 Game 비교값으로 사용하지 않았다.

별도 전체 프레임 기록의 평균 엔진 FPS와 프레임 시간 중앙값:

| 구성 | 상태 | 평균 엔진 FPS | 프레임 시간 p50 |
|---|---|---:|---:|
| Debug | stopped | 59.9 FPS | 16.692 ms |
| Debug | playing | 217.6 FPS | 4.248 ms |
| Debug | returned | 60.2 FPS | 16.682 ms |
| Release | stopped | 60.1 FPS | 16.675 ms |
| Release | playing | 1273.3 FPS | 0.844 ms |
| Release | returned | 59.9 FPS | 16.672 ms |

## 검증 및 산출물

- Debug/Release 최종 빌드: 오류 0건.
- 스케줄러 독립 Debug/Release 검사 통과: 호스트 틱 흔들림, 60/120 FPS 호스트, 명시적 30 FPS 제한, 긴 지연 후 재개, 제출 실패 재시도, 강제 캡처, GPU credit/우선순위/공정성, clock rollback.
- 각 최종 바이너리: temporal history 계약 2171건, meshlet 계약 73건, 후처리 GPU 픽셀 회귀 통과. DX12 validation 메시지/문제/누락 0건.
- 전체 프레임 기록 6개 모두 complete, unacked 0, dropped events 0.
- 프로젝트 EngineSettings와 사용자 active.workspace는 측정 전 원본과 바이트 단위로 동일하다.

원본과 재현 코드, 실행 파일 해시, 상태·FPS 샘플, 전체 프레임 CEPROF 및 분석 JSON은 `Artifacts/frame-regression-20261023/`에 보존했다. 최종 측정은 `DebugFinal/`, `ReleaseFinal/`, 집계는 `final-results.json`, 빌드 해시는 `fps-build-final/binary-*.json`, 회귀는 `regression-Debug-final/result.json` 및 `regression-Release/result.json`이다.

수정 파일: `LiveViewScheduling.h`, `EnhancedSceneRenderer.cpp`, `SceneViewportOverlay.cpp`, `RenderDebugCommands.cpp`, `LiveViewSchedulingTests.cpp`.
