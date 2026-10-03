# Editor 렌더 설정·진단 표면 정리

결정: 2026-10-01. 사용자가 승인한 여섯 UI/수명 변경의 소유권과 완료 조건이다.
현재 적용 범위와 후속 구현을 구분한다. 기존 MAT-8 완료를 재질의 전체 설정 시스템 완료로 확대하지 않는다.
현재 빌드·실제 Editor HTTP 조작·HDR attachment 검증 범위는
[EditorRenderingSurfacesValidation.md](../analysis/EditorRenderingSurfacesValidation.md)에 기록한다.

## 1. 배치와 현재 적용 범위

| 표면 | 책임 | 현재 적용 | 후속 완료선 |
|---|---|---|---|
| Material Node Editor | 현재 Scene의 MeshRenderer graph 저작 | 성공한 Scene 변경에서 활성 문서·선택·preview 해제, 현 Scene 문서만 목록에 노출. 미저장 문서는 메모리에 유지하고 `Saved/Editor/MaterialDrafts`에 복구 사본 저장 | W9: 복구 사본의 명시적 열기/새 자산 저장 UX. Scene 대상 없이 직접 자산 편집하는 모드는 별도 문서 컨텍스트로 추가 |
| Scene Show > SkyBox | 해당 Editor view의 배경 표시 | view request의 HideSkyBox 비트로 HDRI 대신 중립 회색 배경 표시. Game/IBL은 유지. 기본 forest는 배경 off로 시작하고 개발자의 명시적 HDRI 선택은 배경 및 Scene 표시를 켬 | RND-ENV: 제품 Environment 설정 및 편집 가능한 fallback 배경색과 통합 |
| Scene Show > Render Statistics | 빠른 Scene 상태 확인 | 좌측 x, ViewGizmo 높이의 반투명 비상호작용 오버레이. 완료된 Scene 해상도·Scene FPS·backend/ready·동일 view GPU 표본만 표시. 별도 우측 하단 FPS와 중복 방지 | 14-LIVE: view별 CPU/GPU snapshot을 연결하여 CPU 비용도 표시. 표본 없으면 unavailable |
| Profiler > Rendering - Live | 녹화 없이 최신 렌더 진단 | Runtime·전체 view CPU·해당 view GPU/pass 시간·validation을 이전 RenderPass에서 이동. Scene/Game/Material Preview 선택, 완료 frame/view·GPU submission·표본 나이(엔진 frame 수). 다른 view 표본은 unavailable | 14-LIVE: renderer가 각 view의 마지막 완료 표본과 벽시계 age를 보존. 숨긴 view·Scene 교체·resize·backend 지원 차이를 표시 |
| Settings > Preferences | 사용자 편의 설정 | 기존 UI scale·Content Browser tree width의 전용 창 | W9: 로컬 저장소/기본값/복구·다른 프로젝트 독립성 |
| Settings > Project Settings | 공유 제품 설정 | General/Build 기존 편집기 재사용. Graphics는 기존 live tuning과 배경 토글. Quality는 미구현 안내 | W9: default profile/환경 참조와 Quality preset 저장·쿠킹·Player 소비 |
| Window > RenderPass | 실행 구성 설명 | 활성 pipeline 선언을 읽기만 함. 설정/진단은 각각 Project Settings/Profiler로 연결 | 4.3 RG-V: compiled graph viewer. 4.6 CSRP-5/6: C# IR 결과를 같은 viewer에서 표시 |
| Settings > Render Debug | 한 프레임 조사 | Render Frame Debugger 이름과 현재 미지원 안내, Rendering - Live 연결. 비동작 DX11/외부 PIX 필수처럼 보이는 안내 제거 | 14-7 RF0~RF7: 독립 RenderCapture/.ceframe viewer를 동일 stable window ID에서 실제로 열기 |

현재 UI/등록 수정은 후속 시스템의 완료 증거가 아니다. Graphics의 live tuning은 임시 런타임 변경이며
SceneRenderProfile에 저장되는 제품 설정과 혼동하지 않도록 창에 표시한다.

## 2. W9 — Preferences / Project Settings 소유권·저장 계약 (PHASE 21, 미산정)

현재 `EditorSettingsStore`는 개인 Preferences와 Build/RenderPassSettings를 프로젝트
`ProjectSetting/EngineSettings.asset`에 함께 쓴다. 창만 나누는 것으로 저장소 분리를 완료했다고 보지 않는다.

| 위치 | 수집할 기존/추가 항목 | 정본·소비 |
|---|---|---|
| Preferences > Interface | 기존 ImGuiScale, ContentTreeWidth, 폰트/테마·진단 갱신 간격 | 사용자 로컬 설정. 저장된 사용자 배율과 OS DPI는 각각 보존하고 최종 배율의 출처 표시 |
| Preferences > Viewport / Workspace | Show FPS/Statistics/SkyBox/gizmo, 카메라 이동속도·FOV·navigation, workspace layout/preset | Editor 로컬/session 값. Game/Player 또는 Scene 조명에 복사하지 않음 |
| Project Settings > General / Build | 기존 projectName/startupScene/Player backend, scene 목록·build 기본값 | 프로젝트 공유 설정. 패키징 및 Player 시작 경로에서 실제 소비 |
| Project Settings > Graphics | default SceneRenderProfile asset GUID, default Environment asset/cooked 참조, renderer/backend capability 표시 | Asset GUID/상대 경로를 제품 manifest로 resolve. 런타임에서 개발 PC 절대 경로를 요구하지 않음 |
| Project Settings > Quality | 이름 있는 quality tier, SceneRenderProfile 참조, target/backend 선택·미지원 설정 진단 | 같은 설정 schema를 UI/쿠커/Player가 사용. RND-1~3 품질 값을 소비하며 미구현 기능용 가짜 스위치 금지 |
| SceneRenderProfile Inspector | Scene별 post/lighting/environment 조정 | default profile 위에 명시적 Scene override. 적용 우선순위와 해제 복원, Undo/Redo·dirty·저장 규칙을 통일 |

### W9 완료 게이트

1. 설정 inventory의 각 행을 실제 consumer/기본값/저장 경로/적용 시점/재시작 필요 여부와 연결한다.
2. 개인 값과 프로젝트 값의 저장소를 분리하고, 기존 EngineSettings의 알려진 키만 이전한다.
   손상/알 수 없는 키 보존, 쓰기 실패 복구, 새 프로젝트와 재시작 왕복을 검사한다.
3. default profile → Scene override의 단일 resolver를 두고 UI의 임시 live tuning이 제품 SoT를 덮어쓰지 않게 한다.
4. backend는 부팅 고정으로 표시하며 재시작/패키징 전 적용 상태를 알린다. Quality 이름/중복/삭제/참조 실패를 검증한다.
5. Draft 열기는 명시적 사용자 동작이며 원 Scene의 EntityHandle을 새 Scene에 재바인딩하지 않는다.
   다른 Scene·실패한 load·Play/Stop·Prefab context에서 canvas/preview가 섞이지 않음을 검사한다.
6. Editor/Player 빌드, 설정 reload/쿠킹/패키지 실제 소비, UI 100%/150%/좁은 panel·도킹·입력 회귀를 통과한다.

선행: 현재 W3/W4 stable window/context 기반. Environment 제품 필드는 RND-ENV, 품질 지원 범위는
RND-1~3, Pipeline 저작 선택은 CSRP-5/6과 연결한다. W9 전체를 MAT-9 선행으로 붙이지 않는다.

## 3. 14-LIVE — Rendering Live 완료 계약 (PHASE 14, 미산정)

Live는 `.ceprof` reader의 선택 구간과 분리한다. Record/Stop/파일 열기와 무관하게 renderer가
게시한 immutable completed-view snapshot만 읽는다. 현재 전체 CPU 비용을 Scene 비용으로 바꾸어 표기하지 않는다.

- renderer에서 Scene/Game/Preview별 CPU record+submit, GPU pass/queue span, frame/submission/view/
  scene epoch/resize generation/관측 시각을 함께 보존한다. UI가 frame ID로 view를 추정하지 않는다.
- 요청 없음·첫 완료 전·오래된 표본·view 교체·backend timing 미지원은 unavailable/stale/unsupported로 구별한다.
- 상시 page/HUD는 최대 4Hz로 복사하고 닫으면 수요를 중단한다. Record off에서 실제 숫자와 샘플 age가 갱신되어야 한다.
- topology 상세는 RG-V에 링크한다. Live가 별도의 실행 DAG·GPU 자원 소유권을 만들지 않는다.
- 두 view 교대/preview pin/Scene 전환/resize·pause·Record on/off·`.ceprof` load 중 동일 귀속 확인,
  Debug/Release UI 동작, 페이지 off/on CPU/GPU 비용·메모리 상한 실측을 완료 gate로 둔다.
- 2026-10-01 관측 기록: Release Vulkan offscreen fixture에서 새 설정 창의 UI frame은 진행하지만
  `render.live.fence` 또는 `dx12.live` HTTP operation이 120초 내 완료되지 않았다. 원인은 미확정이다.
  이 Vulkan 재현·원인 규명·비교는 [PHASE 4.9](BackendParityPlan.md)로 이관하며 14-LIVE 완료 조건에서 제외한다.
  DX12의 view 귀속·UI 동작·비용 검증은 위 조건대로 유지한다.

## 4. 추적과 누락 방지

대시보드의 W9(21)·14-LIVE(14)는 미산정이며, RG-V(4.3)는 4일·RND-ENV(4.75)는 10일로 2026-10-01 산정했다.
CSRP-5/6의 viewer 연결 및 14-7의 메뉴 착지는 기존 해당 행의 완료 조건에 편입한다.
개별 후속 항목을 닫을 때 이 문서의 표와 canonical plan, dashboard 상태·검증 결과를 함께 갱신한다.
현재 4.25의 40일과 MAT-9의 완료 조건을 이 UI 재배치의 미산정 비용으로 바꾸지 않는다.
