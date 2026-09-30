# MAT-9 노드 편집기·인스펙터 성능 측정

측정일: **2026-09-30**. 별도 세션 「프로파일링으로 프레임 급감 원인 찾기」의
실제 창 조작·HTTP·프로파일 결과를 PHASE 4.25 기록으로 통합한다.
수치는 reflgen 병합 **전** 측정이며, 병합 후 재측정 결과로 해석하지 않는다.

## 수정과 소스 확인

- LX 캔버스의 핀·열 너비 등 반복 계산을 캐시하고 그리기 과정의 중복 작업을 줄였다.
- 노드 편집기의 미리보기 패널을 선택 노드 설명·소켓 정보 패널로 바꿨다.
- MeshRenderer Inspector에 머테리얼 검색·선택·에셋 추가와 기본 수치 편집을 연결했다.
- 구체·격자 바닥 미리보기는 Inspector에서 기본 접힘으로 제공한다. 재질 변경·Refresh에
  갱신하고 완료 결과를 재사용한다. 노드 창을 닫은 상태에서도 Inspector 미리보기는 유지한다.
- `ImGuiDrawHelperMeshRenderer.cpp`의 `DrawInspectorPreview()` 호출과
  `MaterialGraphWindow.cpp`의 독립 Inspector preview 요청을 현재 소스에서 확인했다.
- 해당 세션이 기록한 최종 소스 8개의 SHA-256은 통합 전 작업 트리와 모두 일치했다.

## 실제 실행 측정

CreatorRobot, Scene **1302×614**, 워밍업 후 약 18~20초 구간이다.
노드 편집기와 Inspector를 표시하고 프로파일러 창은 닫았다.
FPS는 구간 전체에서 완료된 Scene frame 수를 시간으로 나눈 값이다.

| 조건 | 완료 Scene FPS | 캔버스 CPU 평균 |
|---|---:|---:|
| Debug 수정 전, 미리보기 접힘 | 28.99 | 미계측 |
| Debug 수정 전, 미리보기 펼침 | 29.12 | 미계측 |
| Debug 계측 추가, 수정 전 | 31.88 | 26.34 ms |
| Debug 수정 후, 미리보기 접힘 | 43.06 | 9.27 ms |
| Debug 수정 후, 미리보기 펼침 | 41.71 | 9.70 ms |
| Release 수정 후, 미리보기 접힘 | 310.99 | 0.697 ms |
| Release 수정 후, 미리보기 펼침 | 330.22 | 0.618 ms |
| Release 기록 중지, 미리보기 펼침 | 342.67 | 미계측 |

Presentation의 긴 CPU 구간을 DXGI Present 자체의 비용으로 분류하면 안 된다.
실제 DXGI Present는 평균 0.35~0.37 ms였고, 캔버스 계산과 Scene 잠금 대기가
프레임 경로를 지연시켰다. Debug 렌더 스레드 CPU 약 23 ms는 이 창 배치에서 남는다.
Release 보관 프레임 제한으로 저장 표본에는 공백이 있다. CPU/GPU 평균은 수집된
유효 구간, FPS는 전체 측정 구간의 완료 프레임 수를 사용한다.

이 조건은 [MAT9MaterialScenePerformance](MAT9MaterialScenePerformance.md)의
작은/큰 SceneView CPU 측정 조건과 다르므로 FPS 숫자를 직접 이어 붙이지 않는다.
GPU/GBuffer/tier별 최종 성능 상한과 이동 카메라 수용을 대신하지 않는다.

## 검증과 한계

- Lattice Debug 자체 검사 통과.
- Material Node Editor 제품 회귀: Debug **1,341개**, Release **1,339개** 검사,
  각 실제 capture 5회, source drift 0, 정상 종료. GPU 검증 문제 0건.
- 실제 UI의 에셋 추가·재선택·Undo·노드 창 종료 후 미리보기 유지 확인.
- 임시 검증 에셋 제거·Release workspace 복원·검증 Editor 정상 종료 확인.
- 별도 `editor.windows` 등록 감사는 MaterialGraph 창을 bodyless 1건으로 보고했다.
  실제 그리기와 제품 회귀가 통과했어도 이 감사 항목을 해결된 것으로 세지 않는다.
- MAT-8은 완료 상태를 유지하고 MAT-9는 진행 상태를 유지한다. ImGui scale 최초 0.8
  원인 분석은 사용자가 미뤄 둔 별도 후속 항목이다.

보존한 집계·소스 identity·회귀 종료값은
[MAT9NodeEditorPerformance.json](MAT9NodeEditorPerformance.json)에 있다.
원시 `.ceprof` 및 임시 workspace·검증 에셋은 제품 소스 커밋에 넣지 않는다.
