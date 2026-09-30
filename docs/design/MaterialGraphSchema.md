# MAT-2 Material 문서와 typed IR

**2026-09-28 · 독립 예제 Debug/Release 검증 완료.** Editor 창 연결은 LX-3,
Slang 생성은 MAT-6, 자산 등록·cook·runtime generation 연결은 MAT-7에서 진행한다.
이 문서의 generation은 마지막 정상 **IR**이다. GPU shader generation은 아직 생성하지 않는다.

## 소유권과 API

- `Lattice/Material/LXMaterialGraph.h/.cpp`: `LXMaterialAsset`, 소켓/노드 메타데이터,
  Blackboard와 `.shadergraph` archive.
- `LXMaterialNodes.cpp`: 고정 Blender 5.1.1 소켓 자료에서 생성한 초기 정의.
  `Tools/blender/export-lx-material-schema.ps1`로 재생성하며 저장소 `.clang-format`을 적용한다.
- `LXMaterialIR.h/.cpp`: 검증된 asset → 값 스냅샷 `LXMaterialIR`,
  마지막 정상 IR 게시, Material asset 전체의 편집·Undo/Redo.
- Core의 `LXGraph`, 정의 레지스트리, 그룹·연결·배치 검증을 재사용한다.
  ImGui·Engine·Editor 타입이나 RHI handle은 Material 계층에 포함하지 않는다.

Material host는 `LXMaterialDocument::Edit`에서 asset 전체를 변경하고
`LXMaterialAsset::CollapseToGroup`을 사용한다. 그룹 생성 시 Core가 제공하는
이전 ID → 새 ID 대응으로 노드 revision과 소켓 색 공간·표시 상태도 이동한다.
Material의 Undo/Redo는 Blackboard와 이 메타데이터까지 함께 복원한다.
Core `graph.Undo()`만 사용하면 Material의 부가 상태를 되돌릴 수 없으므로
LX-3의 Canvas/명령 adapter는 이 Material transaction에 연결해야 한다.

## `.shadergraph` schema 1

UTF-8 JSON으로 저장하고, 기존 pinned rapidyaml로 읽는다. C++ 독립 예제에는
기존 `ryml.lib`·`c4core.lib`를 연결했다. 파서 오류는 객체별 예외 callback으로
처리하며 전역 callback이나 `abort()`를 사용하지 않는다.

| 레코드 | 보존 내용 |
|---|---|
| 문서 | `kind=LatticeMaterial`, `schemaVersion=1`, `domain=material`, `graphId`, `activeOutput` |
| Blackboard | 안정 ID·identifier·이름·타입·typed default·색 공간·노출 여부 |
| graph | domain·ID allocator·node·link·group |
| node | 안정 ID·definition ID·schema revision·제목·group 참조·순서 있는 socket·속성·동적 핀 규칙 |
| socket | ID·identifier·이름·방향·타입·복수 연결·동적 여부·interface ID·default·활성/숨김/값 숨김·색 공간 |
| group | 안정 definition ID·이름·순서 있는 interface·내부 graph. 복사본은 같은 ID와 내용을 공유 |
| layout | node 위치·접힘·frame 소속, frame 제목/좌표/크기, view 중심/zoom |

ID는 문서/그룹 scope 내 `uint64_t`다. 문서의 root graph ID와 그룹 ID를 IR의
source scope로 유지한다. 제품 자산 GUID와 cook identity는 MAT-7에서 연결한다.
값은 bool/int64/double/vector3/color4/string/none으로 태그를 붙인다.
double과 float 배치는 충분한 round-trip 정밀도로 저장한다. Texture는 자산 참조,
Sampler는 sampler 기술의 참조 문자열이며, Surface/Closure는 literal을 받지 않는다.
`data`·`linear`·`srgb` intent를 보존한다. 실제 색 변환과 resource binding은 MAT-6 이후의 책임이다.

새 `Sampler`·`Closure` 핀은 기존 Core enum의 마지막에 추가했다. 예제 저장 형식은
`LXG 9`로 올라갔고 LXG 1~8 읽기를 유지한다. `.lxg`에는 Material 메타데이터를
담지 않으므로 `.shadergraph`를 대신하지 못한다.

## 정의와 IR 검증

초기 Blender 정의는 RGB, Value, Image Texture, Normal Map, Principled BSDF,
Material Output 여섯 개다. 입력/출력의 순서·identifier·표시 이름·default·표시
상태는 고정 `blender-5.1.1-shader-nodes.json`에서 생성한다. Principled는
31개 입력과 BSDF Closure 출력을 갖는다. 여기에 엔진 전용 typed Parameter와
Reroute 기본형을 제공한다. Blender 전체 노드나 모든 enum 조합의 지원 완료를 뜻하지 않는다.

IR 전에 domain, 등록 정의와 revision, socket schema/default, color intent,
enum 속성, 파라미터 참조·타입, 연결 방향·타입·fan-in·cycle, 활성 socket,
group interface·공유 복사본·참조 순환을 검사한다. 정상 Output 노드가 선택되어야 한다.
group 최대 깊이는 16이다. archive는 16 MiB·100,000 tree entry 상한,
중복 필드·태그·alias·비유한 수·알 수 없는 필드 거부를 적용한다.

IR의 node 순서는 의존성이 먼저 나오며 동률은 안정 ID로 정한다. link와
Blackboard, group scope도 안정 ID로 정렬한다. 소켓과 group interface 순서는
저장된 정의 순서를 유지한다. root와 공유 group을 각각 한 번 내린다.
IR에는 node/socket/link/interface ID, 타입·값·색 공간·속성·parameter/group
참조가 있고 위치·제목·접힘·Frame·view는 없다. 배치만 바뀌면 같은 IR generation을 사용한다.
Slang opcode·resource table·feature 추출·semantic cache identity는 MAT-6에서 추가한다.

## migration과 실패 복구

- 검사 입력 schema 0은 revision/소켓 identifier/color-space 필드가 없던 경우다.
  등록된 정의의 순서에서 identifier를 복원하고 revision 1, Color=`linear`,
  나머지=`data`로 명시적으로 승격한다. 모르는 schema 0 정의는 자동 추측하지 않는다.
- 미래 문서 버전·잘못된 domain·등록 정의의 revision/schema/enum 불일치는
  재개방을 거부한다. 정상 `.bak`가 있어도 호환성 오류를 과거 파일로 숨기지 않는다.
  실패한 Reload는 열린 asset과 마지막 정상 IR을 모두 유지한다.
- 미등록 node는 전체 archive record와 extension 필드를 정규화된 JSON payload로
  보존한다. 데이터 편집을 막고 저장·재개방은 허용한다. payload의 값과
  타입을 보존하며 whitespace까지 byte 단위로 보존하는 계약은 아니다.
  모르는 socket 타입 자체는 호환성 오류다. 미등록 node가 있으면 IR 게시를 거부한다.
- Save는 별도 파일 쓰기 → flush/닫기 → 엄격 재개방 → 전체 asset 동등성 비교 →
  유효한 기존 primary 백업 → Windows 원자 교체 순서다. 검증/교체 실패 시 primary를 유지한다.
  잘린 primary를 복구할 때 기존 유효 backup을 손상 primary로 덮지 않는다.
- 파일 손상은 유효 `.bak`로 복구할 수 있다. IR 생성 실패는 generation과
  마지막 정상 IR을 바꾸지 않는다. Shader/GPU 교체 복구는 MAT-6/MAT-7에서 이어받는다.

## 검증 결과

VS18/v145 x64 Debug/Release에서 독립 예제를 빌드하고 모두
`LX_MATERIAL_TEST_OK checks=95`, `LX_SELF_TEST_OK`를 확인했다.

검사는 정확한 숫자·한국어/escape·배치·접힘·Blackboard·이미지/normal/resource·
group 재사용/중첩/ID 이동/Undo/Redo 왕복, IR 결정성·배치와 IR 분리,
future schema·node revision·unknown node·잘린 파일·교체 잠금 실패와
이전 문서/IR 유지, 타입/활성 socket/enum/parameter 오류를 포함한다.
기존 LX 도메인·조작·저장과 숨긴 ImGui 검사도 함께 통과했다.

증거는 `Build/LatticeExample/mat2-{debug,release}-{build,tests}.log`다.
고정 `.shadergraph` 예시는 `Tools/LatticeExample/fixtures/material/`에 있다.
실제 Editor의 `.shadergraph` 조작, DX12/Vulkan Canvas 결속과 실시간 HTTP CLI는
LX-3/LX-3H에 남는다.

MAT-6은 이 typed IR을 [결정적 Slang/metadata](MaterialSlangCodegen.md)와 실제
node/socket compiler 진단으로 낮춘다. Material 그룹 생성 시 unlinked disabled/implicit
입력은 내부 기본값으로 남겨 의미를 보존한다. 마지막 정상 CPU program과 compiled artifact를
함께 유지하며, 실제 RHI/PSO/Scene 자원 교체는 MAT-7이다.

대시보드 JavaScript 전체 파싱은 `broken=0`이고 MAT-2 완료·Material 공수
10/34를 별도로 확인했다. 기존 `verify-plan-dashboard.ps1`은 `days:null` 행
24건과 PHASE 4.6 metadata 한 건을 오류로 판정한다. HEAD 기준 파일에서도
같은 결과를 재현했으므로 전체 검사 통과로 기록하지 않는다.
