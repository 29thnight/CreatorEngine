# Editor 렌더 표면 적용 검증

2026-10-01. 사용자가 승인한 여섯 UI/수명 변경의 즉시 적용 슬라이스 검증이다.
후속 소유권과 완료선은 [EditorRenderingSurfacesPlan.md](../plans/EditorRenderingSurfacesPlan.md),
환경 의미는 [EnvironmentRenderingPlan.md](../plans/EnvironmentRenderingPlan.md)에 고정했다.

## 적용

- 성공한 Scene 변경에서 Material Node Editor의 활성 문서·선택·preview를 해제한다.
  미저장 그래프는 기존 자산에 덮어쓰지 않고 `Saved/Editor/MaterialDrafts`에 복구 사본을 보존한다.
- Scene Show의 SkyBox는 해당 view에서만 배경을 숨긴다. Graphics의 배경 토글은 전체 view에 적용한다.
  GT frame packet에 값을 밀봉하고 DX12/Vulkan 공통 SkyBox node에서 소비한다.
- SkyBox의 깊이 쓰기를 제거했다. SSGI의 축소 Hi-Z cell hit 뒤 full-resolution GBuffer normal이
  비어 있는 하늘 픽셀을 걸러 배경색을 간접광으로 가져오지 않게 했다.
- Render Statistics는 Scene 좌측/ViewGizmo 높이의 작은 비상호작용 오버레이로 옮겼다.
  Scene 완료 해상도/FPS/backend/ready와 같은 view의 GPU 표본만 표시한다.
- 상세 런타임/비용/Pass/validation 정보는 Profiler의 Rendering - Live로 옮겼다.
  capture reader와 Record 요청 없이 renderer snapshot을 읽는다. CPU는 전체 view 합계로 표시한다.
- Settings의 Preferences/Project Settings 창을 분리했다. 현재 저장소는 기존 EngineSettings를 재사용한다.
  RenderPass는 활성 선언 구조, Render Debug는 독립 Frame Debugger의 미구현 안내와 Live 진입을 표시한다.

## 빌드·정적 검증

VS18/v145의 `Editor/CreatorEditor.vcxproj` 및 종속 프로젝트 빌드가 Debug/Release 모두 성공했다.
최종 소스의 SSGI Slang은 아래 실제 제품 실행에서 새 프로젝트의 shader cache로 컴파일했다.

| 검증 | 결과·증거 |
|---|---|
| Debug 빌드 | `Build/Obj/editor-organization-debug-final.log`, exit 0 |
| Release 빌드 | `Build/Obj/editor-organization-release-final.log`, exit 0 |
| Dashboard 전체 파싱 | `Tools/regression/verify-plan-dashboard.ps1 -RequireFullParse`: 439행, string/shape/meta 오류 0, 백분율 finite |
| 창 등록 | 실제 `editor.windows`: 28개, orphan/bodyless/duplicate/empty 오류 없음 |
| 변경 형식 | `git diff --check` 통과 |

Release의 기존 `/DELAYLOAD:vulkan-1.dll` LNK4229 경고는 남아 있다. 빌드 성공을 모든 backend의
실제 실행 성공 또는 새 UI의 수동 시각 검증으로 읽지 않는다.

## 실제 Editor 조작

체크인된 `Tools/regression/verify-editor-rendering-surfaces.ps1`은 기존 MAT-9 fixture를 새
프로젝트로 복사하고 제품 Editor를 HTTP로 조작한다. 사용자 프로젝트/프로세스는 수정하지 않는다.

| 실행 | 결과 |
|---|---|
| Debug / DX12 | `Build/Obj/EditorRenderingSurfaces/Debug-DX12-final`: 종료 포함 22개 단정 통과, 정상 종료 |
| Release / DX12 | `Build/Obj/EditorRenderingSurfaces/Release-DX12-final`: 종료 포함 22개 단정 통과, 정상 종료 |
| Release / Vulkan | `Release-Vulkan-v1/v2`: 설정 창 focus 후 GPU fence HTTP operation 미완료. `Release-Vulkan-v3`: UI frame 진행과 Record stop 확인, 다음 live 조회가 120초 내 미완료. 전체 gate 미통과 |

검사 내용: 새 창 focus/close와 본문 배선, Show FPS/Statistics/SkyBox 변경,
Scene/Game 실제 attachment capture, 미저장 graph 편집, 존재하지 않는 Scene load 실패 시 현재
문서 유지, 새 Scene에서 이전 문서 비노출, 이전 document apply 거부, 복구 사본 실제 파일 생성.
복구 파일명은 session별 UUID를 포함해 프로세스 재시작 뒤 Scene/document ID가 재사용되어도
이전 사본을 덮어쓰지 않는다. 기존 사본 sentinel을 넣고 그대로 유지되면서 새 사본이 생성됨을 검사했다.
창 focus 검증은 viewport가 가려질 수 있으므로 Scene GPU frame 대신 실제 UI frame 진행을 확인한다.

두 구성 모두 `profile.pause` 후 `profile.stats.state=stopped`를 확인한 상태로
Rendering - Live를 열었다. GPU collect가 Debug 20 → 53, Release 25 → 190으로 증가해 Record off에서도
새 완료 표본이 생성되었다. 상세 응답은 해당 case의 `responses.jsonl`과 `recorder-off.json`이다.
당시 `result.json`은 종료 단정 이전에 작성해 checks=21이며, stdout의 checks=22는 종료 단정을 포함한다.
하네스의 결과 파일 작성은 이후 종료 단정 뒤로 옮겼다.

## 고정 HDR 픽셀 비교

`Tools/regression/compare-editor-environment-visibility.py`는 controlled capture의 float32
`preToneHdr`를 비교한다. Scene overlay가 나중에 깊이를 기록하므로 mesh mask는 GBuffer normal의
유효 표면으로 판정한다. 332×202의 55,416개 mesh / 11,648개 background 픽셀을 비교했다.

| 조건 | Debug DX12 mesh max abs | Release DX12 mesh max abs | 배경 max abs |
|---|---:|---:|---:|
| Scene view-only 배경 off | 0 | 0 | 1.5537109375 |
| 전역 배경 off | 0 | 0 | 1.5537109375 |
| 배경 on 복원 | 0 | 0 | 0 |

Scene에서만 숨긴 뒤 Game HDR max abs는 0이다. 배경 변경 전후 IBL generation은 1로 유지됐다.
모든 attachment는 finite이며 normal/coverage도 동일하다. 이는 해당 fixture의 픽셀 검증이며,
모든 재질 lobe와 Player 패키지의 환경 기능 완료 증거가 아니다.

## 남은 완료선

- **21 W9:** 개인/프로젝트 저장소 분리, 설정 inventory consumer, default profile/Quality
  저장·쿠킹·Player 소비, 복구 사본 명시적 열기 UX, 100%/150%·좁은 panel·도킹 수동 검증.
- **14 14-LIVE:** per-view CPU/GPU immutable snapshot, wall-clock age/epoch/resize·stale/unsupported,
  Record on/off·파일 reader·멀티뷰 전체 회귀, HUD/page off/on 비용 실측.
- **4.3 RG-V / 4.6 CSRP-5/6:** compiled graph 읽기 전용 viewer와 C# immutable IR 표시 세대 일치.
- **4.75 RND-ENV:** profile Environment schema/resolver, diffuse/specular 별도 enable/intensity,
  fallback 색의 편집/배경 세기, Standard/LX·Forward/Deferred·Editor/Player 전체 route와 쿠킹/캐시/성능.
- **14-7 RF0~RF7:** 독립 RenderCapture/.ceframe과 실제 Render Debug 진입.

새 행은 progress/todo이며 공수는 `days:null`이다. UI 재배치를 MAT-9/후속 시스템 전체 완료로
표시하지 않았고 성능 수치는 별도로 측정하지 않았다. 커밋/푸시는 이 요청의 범위에 없다.

## 2026-10-01 추가: 중립 Grid·회색 배경·HDRI 선택 정책

기존 표의 검정 off 배경은 당시 캡처 결과다. 현재 소스는 SkyBox를 숨겨도 동일한 깊이 읽기 전용
패스에서 linear RGB=(0.18,0.18,0.18)을 채우며, Scene post chain이 표시 색으로 변환한다.
기본 바닥 Grid의 선/셀은 중립 회색으로 변경했고, 원점/축에 별도 색을 넣지 않는다.
Resource `forest.ceibl` bootstrap은 배경 off + IBL on으로 시작한다. 명시적 HDR/ceibl 선택은
Core에서 source/배경 on을 함께 발행하고, Editor 드래그/HTTP 소비자는 Scene hide override도
해제한다. 사용자가 forest를 명시적으로 다시 고른 경우도 on이다. 제품 profile을 통한 저장된
환경 선택의 재시작 복원은 ENV-B 범위로 유지한다.

| 검증 | 결과·증거 |
|---|---|
| Debug 제품 빌드 | `Build/Obj/skybox-neutral-debug-final.log`, exit 0 |
| Release 제품 빌드 | `Build/Obj/skybox-neutral-release.log`, exit 0 |
| 실제 Debug / DX12 | `Build/Obj/NeutralEnvironment/Debug-DX12-final`: HTTP 단정 11개, 캡처 11개, 정상 종료 |
| 실제 Release / DX12 | `Build/Obj/NeutralEnvironment/Release-DX12-final`: HTTP 단정 11개, 캡처 11개, 정상 종료 |
| 기본/사용자 HDRI 정책 | 시작 `forest.ceibl/background=false/IBL generation=1`. 사용자 `autumn_field_puresky_1k.hdr` 선택 시 전역 background와 Scene 표시 모두 true. 명시적 forest 재선택도 true |
| 거부 경로 | 존재하지 않는 HDR와 손상된 ceibl 거부. 이전 source/background/Scene hide 상태 유지 |
| 캐시·조명 | background 토글 전후 IBL generation 1→1. forest와 사용자 HDRI 모두 메시 pre-tone HDR max abs=0. Scene-only 숨김의 Game HDR max abs=0 |
| 회색 배경 | 각 구성에서 모든 캡처 attachment finite. 빈 씬 67,064개 배경 픽셀의 linear 0.18 최대 오차 0.00006836 (RGBA16F 양자화). 표시 배경 최소 RGB 약 0.302로 검정이 아님 |
| 실제 Grid | 빈 씬의 깊이를 기록한 16,234개 Grid 픽셀에서 표시 RGB 채널 차이 최대 1/255. 별도 축 색상 없음 |
| 문서/대시보드 | 전체 파싱 439행, shape/string/meta 오류 0·백분율 finite. `git diff --check` 통과 |

하네스는 `Tools/regression/verify-neutral-environment.ps1`, 픽셀 검사는
`Tools/regression/compare-editor-environment-visibility.py <case> --neutral`이다.
표시 배경 검사는 FXAA(spanMax=8)가 메시 색을 섞는 경계 8픽셀을 제외한다. HDR 회색 값과 메시
조명은 경계를 포함한 전체 GBuffer normal mask로 검사한다. 두 구성의 `visibility-pixels.json`에
실제 수치를 남겼다. 드래그 경로는 동일 Core 선택 및 Scene 표시 API 배선을 확인했고, 이번 실제
선택 조작은 HTTP로 수행했다. Vulkan 실제 실행·사용자 HDRI 저장/재시작·후속 IBL weight·표시 색
편집·성능 수용 완료로 확대하지 않는다.
