# LX Material 제품 통합 검증

정본: [BlenderMaterialGraphPlan](../plans/BlenderMaterialGraphPlan.md)의 MAT-7.
이 문서는 고정한 마지막 종료 묶음인 실제 Editor Scene/Game 상태·수명과
native Vulkan 전체 Scene 실행을 소유한다. artist 노드 편집·preview는 MAT-8,
Blender rendered parity와 성능 수용은 MAT-9의 범위다.

## 1. 실제 Editor 경로

`verify-material-editor-integration.ps1`은 독립 프로젝트를 쿠킹하고 full
`CreatorEditor.exe`를 실행한다. `--smoke-offscreen`은 창 표시와 로딩 창만 생략한다.
실제 Scene/Game, ImGui frame loop, RenderThread, D3D12 디바이스와 GPU validation은 동작한다.
마우스를 이동하거나 다른 창의 포커스를 가져오지 않는다.

HTTP command service의 `material.graph`는 활성 Scene의 EntityHandle을 해석하여
MeshRenderer의 inline 재질을 편집한다. 후보 instance를 검증한 후 기존 Undo 시스템을
통해 `SetMaterial`로 게시한다. 잘못된 그래프·parameter ID·타입·비유한 값과 잠긴 entity는
게시 전에 거부한다. base material asset에 연결된 instance는 이 명령에서 편집하지 않는다.
이 명령의 성공을 artist canvas의 마우스 조작 완료로 해석하지 않는다.

`render.live.fence`와 `render.live.capture`는 프레임을 계속 실행하면서 최종 HTTP 결과를
대기한다. capture manifest는 SceneHost가 선택한 ready instance 또는 마지막 정상 owner의
graph GUID·generation·uniform bytes·texture GUID·model/mesh generation을 기록한다.
PSO 준비 전 빈 프레임을 성공으로 세지 않으며 요청한 owner가 실제 frame에 나타날 때까지
제한 시간 내 재시도한다.

검증 항목:

- Core 그래프 적재와 typed IOR 수정, 실제 HDR 변화.
- Undo/Redo의 instance와 GBuffer·HDR 복구.
- unknown parameter·잘못된 타입·NaN·없는 graph의 거부와 기존 픽셀 보존.
- 손상된 graph reload 실패 후 마지막 정상 instance·픽셀 보존.
- Layered 그래프 교체와 실제 HDR 변화.
- Scene 저장·새 Scene 전환·재개방 후 instance·픽셀 복구.
- Play 중 수정과 Game 렌더, Stop 후 authoring Scene·픽셀 복구.
- entity 삭제·Undo 후 재질 owner·픽셀 복구.
- cooked Derived payload/sidecar 바이트 보존, validation ledger와 정상 종료.

픽셀 비교는 baseColor·metalRough·normal·emissive·depth·preToneHdr의 전체 float 데이터를
읽는다. 복구 허용 오차는 최대 정규화 절대 오차 0.003이다. capture별 비유한 성분과
WARNING 이상 GPU validation은 0이어야 하며 validation layer가 켜지고 실제 drain되어야 한다.
quit은 HTTP 요청 수락 후 프로세스 종료 코드 0을 확인한다. 종료 뒤 operation poll 응답을
받는 것을 요구하지 않는다.

## 2. native Vulkan 경로

`MaterialVulkanSceneProbe.vcxproj`는 기존 Scene 회귀의 같은 geometry·material·CPU reference를
native Vulkan device/resource table/pipeline/encoder로 실행한다. SPIR-V 생성 확인만으로
런타임 성공을 판정하지 않는다. `verify-material-vulkan-scene.ps1`이 Debug/Release에서
Scene·Shadow/Decal·SSS·Refraction·Volume·encrypted cooked Scene을 실행한다.

validation layer가 켜져 있어야 하고 readback 및 shutdown까지 WARNING 이상 메시지와
encoder unsupported/drop은 0이어야 한다. cooked 실행에서는 Scene Slang job 제출이 0이어야 한다.
검증의 transport·SSS·Volume은 각 Scene 문서에 정의한 유한한 근사 범위를 유지한다.

통합 검증에서 수정한 문제:

- Editor asset database가 `Assets/Derived`의 cooked sidecar를 고아 `.meta`로 삭제하던 문제.
  초기 scan과 watcher create/modify/delete/move에서 cooked artifact 영역을 import하지 않는다.
- Vulkan CPU texture upload가 cube/array를 거부하던 문제. 각 slice/mip의 정렬된 복사와
  array/cube metadata를 보존하여 기존 환경 cube의 native sampling을 사용한다.
- Scene material host의 resource array SPIR-V와 Vulkan의 register별 descriptor layout 불일치.
  host texture 입력을 register별 독립 선언으로 고정하며 기존 RHI binding 계약을 유지한다.
- 큰 뷰포트의 여러 dispatch가 Vulkan descriptor pool을 소진하여 IBL 계산을 누락하던 문제.
  같은 recording version에 pool을 추가하고 기존 set은 completion까지 유지한다. 재획득 때
  version의 모든 pool을 reset한다. 1920×1080의 sequential/worker 프레임을 실제 픽셀로 검증한다.
- cooked SPIR-V GBuffer가 공통 함수의 runtime boolean 뒤에 남은 Decal 자원을 요구하던 문제.
  Decal 이전 GBuffer의 생성 경로를 분리하고 coverage·normal·medium 평가만 공유한다.
  GBuffer bytecode에 Decal descriptor 접근이 없고 새 encrypted cook의 실제 실행이 통과해야 한다.
- native probe의 pipeline cache 연결 누락과 AO/shadow fixture의 render-target bind 누락.
  제품 알고리즘을 바꾸지 않고 probe가 실제 Vulkan command 계약을 사용하도록 수정했다.

## 3. 실행 증거

2026-09-29 VS18/v145 full CreatorEditor Debug/Release 빌드와 실제 Editor 검증이 통과했다.
각 구성 1,430개 검사·11개 최종 capture·정상 종료 코드 0이며 1,225개 source SHA-256 변경은 0개다.
GPU validation은 두 구성 모두 `layerEnabled=true`, `mode=gpu`, `problems=0`,
`droppedMessages=0`이고 실제 drain은 Debug 11,358회·Release 20,803회다.
GBuffer의 Decal 경로 분리 뒤 정본으로 재실행한 결과다. Release 준비의 cooker loader
error 32는 cooker 빌드와 실행이 겹친 파일 잠금이었다. 빌드 완료 뒤 재실행하여 통과했다.

Editor 증거:

- `Build/Obj/MaterialProductProbe/editor-integration-Debug-gate.log`
- `Build/Obj/MaterialProductProbe/editor-integration-Release-gate.log`
- 각 `editor-integration-<configuration>-root.txt`가 가리키는 격리 프로젝트의
  `source-hashes.json`, `responses.jsonl`, `validation.json`, capture별 `manifest.json`과 float attachments.

DX12 전체 raster/Scene·Shadow/Decal·Volume·굴절·SSS 회귀도 Debug/Release에서 통과했다.
전체 raster는 각 21,079,764개 검사·3,899,966 GPU 성분·Scene 56 frame/generation 12 frame이며
578개 source 변경은 0개다. 이 전체 회귀는 마지막 GBuffer의 Decal 경로 분리 전 결과다.
그 분리 후 정본으로 반복 쿠킹한 바이트 일치·6종 거부·accepted 해시 보존과 encrypted
DX12 cooked Scene 각 24 frame·57,185개 검사·10,746 GPU 성분·Scene compile 0회도 통과했다.
최종 쿠킹 회귀의 source snapshot은 588개 변경 0개다.

최종 native Vulkan Debug/Release 전체 gate도 통과했다. 각 구성의 6개 모드를 별도
프로세스로 시작하고 573개 source SHA-256 변경 0개를 확인했다. WARNING 이상 validation,
encoder unsupported/drop은 readback·device shutdown까지 모두 0이다.

| Vulkan 경로 | frame | 검사 / GPU 성분 (각 구성) |
| --- | ---: | ---: |
| Core/Layered Scene·generation·texture·1920×1080 | 56 (generation 12 포함) | 20,306,256 / 3,250,798 |
| Shadow | 42 | 33,233 / 32,256 |
| Shadow + Decal 누적 | 42 + 114 | 295,802 / 290,898 |
| SSS | 12 | 119,416 / 108,072 |
| Refraction | 24 | 185,069 / 155,877 |
| Volume | 33 | 35,029 / 57,840 |
| encrypted cooked Scene | 24 | 57,212 / 10,746 |

Scene은 stale 결과 1회·마지막 정상 instance fallback 7회·abort 1회를 검사했다.
cooked Scene specialization compile은 0회다. 최종 결과와 source snapshot은
`Build/Obj/MaterialProductProbe/vulkan-scene-gate.log`, `vulkan-scene-source-hashes.json`,
`vulkan-scene-<configuration><mode>.log`가 소유한다.

## 4. 종료 상태

2026-09-29 MAT-7의 고정한 세 종료 묶음인 제품 render 연결, 자동 cook/package·Player,
실제 Editor·native Vulkan 통합 검증을 모두 완료했다. PHASE 4.25의 MAT 10행 34일 중
완료는 29일이다. MAT-8 artist preview·비용 badge·fallback 설명과 MAT-9 rendered parity·
성능 수용이 남는다. LX canvas/HTTP의 범위는 LX-3/LX-3H가 계속 소유한다.

2026-09-29 LX-3의 [MaterialNodeEditor.md](MaterialNodeEditor.md)에 MeshRenderer 진입과
Blender형 제품 문서 창을 추가했다. Save/Apply와 새 Editor 재개방을 지원하기 위해
DataSystem의 Editor authoring 경로는 `.shadergraph` source가 cooked 산출물보다 새롭거나
산출물이 없으면 Scene target을 검증·컴파일한다. 실패 시 accepted owner를 유지하며
Player의 catalog/bytecode-only 경계는 그대로다. 이 창의 UI/HTTP 검증은 LX-3/LX-3H
범위이며 MAT-7의 고정 종료 묶음이나 MAT-8/MAT-9 잔여를 늘리지 않는다.

대시보드의 전체 JavaScript 파싱·MAT 10행/34일/완료 29일/잔여 MAT-8·MAT-9 검사와
Vite 문서 빌드는 통과했다. 전체 dashboard checker는 기존 미산정 `days:null` 24행 및
PHASE 4.6 meta 불일치 1건 때문에 실패한다. HEAD와 같은 25건이며 이번 변경을 전체
checker 통과로 기록하지 않는다.
