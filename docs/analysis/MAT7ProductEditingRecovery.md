# MAT-7 제품 편집·실패 복구·쿠킹 회귀

2026-10-02 일곱 번째 단계의 DX12 Debug/Release 제품 통합 기록이다. 이후
[특수 transport 혼합 회귀](MAT7ForwardTransportComposition.md)까지 통과하여 MAT-7은
`done`/미산정이다. PHASE 4.25는 열린 상태이며 BRDF 1024/environment 4096을 유지한다.

## 실제로 수정한 결함

### 명시적 Reload가 손상된 원본 대신 오래된 backup을 읽음

`MaterialGraphWindow`의 Reload가 `LXMaterialAsset::Load`를 호출하면서 `.bak`
복구까지 수행했다. 원본이 손상돼도 성공으로 처리하고 현재 문서를 예전 backup으로
교체할 수 있었다. 명시적 Reload는 primary bytes를 `LXMaterialArchive::Read`로
검사한다. 실패하면 문서 ID/revision과 적용된 재질 generation, 외부 파일을 보존한다.
정상 외부 수정은 Reload→Save→Apply로 실제 Scene 픽셀에 반영한다.

초기 파일 열기의 backup 복구 정책과 명시적 Reload의 실패 정책은 구분한다.

### Player 세대 검증이 저작용 텍스트 파서를 호출함

최초 실제 Player 패키지 실행에서 `runtime.text-parser calls=2`가 확인되어 게시가
거절됐다. generated ShaderMeta를 텍스트로 복구하는 경로뿐 아니라
`GenerationStore::Load`의 `WriteCookedProgram` 재검증도 원인이었다.

- **LXMC/material-program artifact v4**는 generated 계약의 CEDO 바이너리 tree를
  메타·소스·검증된 bytecode와 함께 보관한다. v3는 재쿠킹한다. CEMF schema와
  SceneHost identity 11, shader 수학은 바꾸지 않는다.
- 생성 시 검증한 binary tree를 immutable generated owner에 보관한다. 복구와
  generation 재검증·재쿠킹은 이 tree를 사용한다. Parser telemetry를 우회하거나
  무효화하지 않는다.
- 같은 ShaderMeta schema validator로 검사하며 stable ID·typed defaults·자원·Sampler와
  source SHA-256/generation, pass/backend stage·reflection layout을 다시 대조한다.
  binary default 변조는 정상 caller output을 보존하며 거부한다.

### 임시 검증 프로젝트가 에디터 배율을 0.8로 되돌림

복사한 오래된 fixture의 `EngineSettings.asset`에 `imguiScale: 0.8`이 있었다.
현재 Editor preference는 프로젝트 설정에서 읽으므로 임시 프로젝트 실행에도 이 값이
적용됐다. 실제 `Dynamic_CPP` 프로젝트는 1.5를 유지했다.

임시 에디터에서 1.5를 복구하고 실제 폰트·preference·geometry 적용을 확인했다.
`sync-material-editor-scale.ps1`은 현재 작업 프로젝트의 배율만 fixture에 상속한다.
편집·모델·성능 검증 경로가 이를 사용하며 명시적 성능 측정 배율 옵션은 유지한다.
편집 회귀에는 시작·재시작 시 저장 배율과 실제 적용 배율의 대조를 추가했다.
0.8→1.5 상속 및 다른 설정 bytes 보존도 확인했다. 기본값 변경이나 전역 Preferences
저장소 이관을 수행한 것은 아니다.

## 검증 증거

로그의 공통 위치는 `Build/Obj/MaterialProductProbe`다.

| 검사 | 확인된 결과 |
| --- | --- |
| 실제 node editor Reload/편집 회귀 | strict Reload 수정 후 Debug 1,513 / Release 1,512 checks, 각각 7 captures, source 1,254/drift 0. 값·핀·링크·Undo/Redo·접힘·Save/reopen·외부 수정 충돌·잘못된 원본+정상 backup 거부·정상 외부 재import의 Scene 픽셀 변화·복원·missing texture Apply 실패 보존·새 프로세스 재개방, GPU validation 0. `product-flow-node-editor-*-reimport-final.log`. 이 실행은 v4 포맷 변경 전이며 전체 LX 마우스 제스처/DPI 수용으로 확대하지 않는다. |
| Native UI 조작 | 실제 Windows 에디터에서 배율 0.8→1.5, Roughness 편집, Save, Apply를 클릭했다. 저장 값 0.899999976, dirty false, generation 1→2. `VisualProduct-Debug-a31027a28c13/responses.json`. |
| v4 생성·복구 어댑터 | 72 checks, 실제 DXIL/SPIR-V 컴파일/reflection·typed properties 8·Sampler 1·결정성·source-free 복구·변조 거부·writer/read 재검증의 text parser 0. `product-flow-v4-final-adapter.log`. native Vulkan 실행 검증이 아니다. |
| v4 반복 cook·공통 소비 | Debug/Release 각각 생성 쌍 반복 bytes/source digest 동일, common consumer 26·Code runtime 35·pipeline runtime 212·cooked graphics identity 4 checks. source-free Player 경계에서 compiler 미적재/text parser 0, instance 왕복·실패 보존·warm cache. `product-flow-v4-final-consumers.log`. pipeline cache 실패/수명 검사는 mock cache이며 native GPU 이미지 검사와 구분한다. |
| 실제 v4 encrypted PAK·Player | Debug/Release 각각 programs 3/material 1, Game display 120 promotions, cooked graph ready, Graph Scene compile 0/text parser 0. CEMF entries 17/source identities 115, authoring meta parse 0. 실행 후 runtime entries/PAK SHA-256 보존, 검사 소스 642/drift 0. `product-flow-v4-accepted-package-*.log`. |
| 최종 v4 Editor Scene/Game 회귀 | Debug 1,464 / Release 1,462 checks, 각각 11 captures, source 1,254/drift 0. 시작 시 preference/font scale 1.5 실제 적용 대조, 값/override 픽셀 변화·잘못된 입력/reload 보존·Core→Layered 교체·Scene 저장/reopen·Play/Game/Stop 복원·삭제/Undo·cooked bytes 보존, GPU validation 0. `product-flow-v4-editor-integration-final-*.log`. |
| 최종 빌드 | VS18/v145, Debug/Release RenderEngine·AssetCooker·common consumer·raster probe·Editor static library·CreatorEditor runtime/launcher·Player runtime/launcher 통과. `product-flow-v4-final-*-build.log`. |

실제 패키지 fixture는 authored Core/Layered 두 그래프와 자동 모델 SoT 그래프 하나를
함께 쿠킹한다. 따라서 material programs와 발견된 graph source는 각각 3개다.
화면 24 promotions 조건은 cold graph PSO 준비 전에 종료할 수 있었다.
같은 게시 패키지를 120 promotions까지 실행했을 때 cooked graph
`11111111-1111-4111-8111-111111111111 ready=1 sceneCompiles=0`, text parser 0을
확인했다. 패키지 회귀는 120 promotions와 실제 readiness log를 함께 요구한다.
초기 parser 거부·24 promotions 부족 실행은 성공 증거로 세지 않는다.

에디터 회귀 fixture는 패키지 준비가 갱신한 model sidecar와 과거 Library generation을
같이 복사하면 폐포가 불완전했다. private project에서 공식 `--author-model-asset`
producer로 모델/SoT 그래프 세대를 먼저 준비한 뒤 cook 입력을 수집하도록 수정했다.
실제 작업 프로젝트의 모델/설정은 이 검증에서 변경하지 않는다.

`sceneCompiles=0`은 LX Graph 준비의 카운터다. 모든 코드 셰이더의 최초 준비 비용이나
Player 전체 compiler-free 수용을 뜻하지 않는다. GPU validation이 켜진 이 실행을
평균 FPS·cold start 성능 통과로 환산하지 않는다.

## 남은 범위

일반 alpha와 SSS/refraction/Volume의 혼합 순서·굴절 배경 수용은 후속
[여덟 번째 단계](MAT7ForwardTransportComposition.md)에서 완료했다. SceneHost identity는 12다.
LX 그룹/Blackboard 및 전체 제스처·DPI 마감은 LX-1/3/3H,
Blender 결과 대조와 전체 CPU/GPU·준비 비용은 MAT-9가 소유한다.
Native Vulkan 전체 제품 수용은 PHASE 4.9에 두며 이번에 재실행하지 않았다.
