# Material 문서, typed IR, 편집 트랜잭션

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 소유권과 API

`LXMaterialAsset`는 renderer와 무관한 저작 원본이다. 안정 ID를 가진 `graphId`, `activeOutput`, `LXGraph`, Blackboard, scope별 노드·소켓 상태를 함께 소유한다. scope 0은 루트이고 나머지는 공유 그룹 정의다. 같은 이름의 노드를 표시하는 UI와 실제 연결 식별자인 pin ID / identifier를 혼동하면 안 된다.

`LXMaterialDocument`는 전체 asset을 대상으로 `Edit`, `Undo`, `Redo`, `Reload`, `PublishIR`를 제공한다. `Edit`는 복사한 후보에 작업을 적용하고 검증 오류 또는 무변경이면 거부한다. 성공한 편집은 undo에 이전 asset을 보관하며 최대 128개를 유지한다. 그래프만 바꾸고 Blackboard나 소켓 색 공간을 별도로 저장하는 편집은 이 경계를 깨뜨린다.

## `.shadergraph` schema 1

저장 형식은 `LXMaterialArchive::SchemaVersion == 1`인 LatticeMaterial/material 도메인의 구조화된 archive다. 독립 LX 예제의 LXG 직렬화와 별개다. 노드 정의 revision, stable ID, typed default, enum property, 그룹 interface, `enabled`·`hidden`·`hideValue`, 색 공간 의도를 보존한다. 알 수 없는 노드는 원본 record 전체를 `unknownPayload`에 보존하고 읽기 전용으로 남길 수 있다. 이는 컴파일 지원을 뜻하지 않는다.

파서는 16 MiB, 100,000개 항목, 중첩 64 제한을 적용한다. 중복 map key, YAML tag/alias, 비유한 숫자, 알 수 없는 version/domain/type/color-space와 호환되지 않는 필드를 조용히 받아들이지 않는다. 정의 revision 변경에는 명시적 migration이 필요하다.

## 정의와 IR 검증

`BuildMaterialIR`는 asset 검증 후 활성 `ShaderNodeOutputMaterial`를 요구한다. 각 scope를 위상 정렬하며 동률은 node ID, 링크는 link ID로 정렬한다. 연결 방향·타입, inactive 소켓 연결, 순환, 알 수 없는 정의, revision 불일치, Blackboard ID/타입 불일치를 오류로 다룬다. 그룹 내부도 별도 scope로 lowering된다.

`valueSource=socket`인 parameter는 해당 typed socket default를 Blackboard 초기값으로 반영한다. 같은 parameter ID를 여러 노드가 서로 다른 값으로 정의하면 오류다. 레이아웃 이동은 의미 IR과 분리되며 같은 IR을 다시 게시해도 generation은 증가하지 않는다.

IR 성공은 GPU 준비 성공과 다르다. UI에 보존 가능한 unknown node가 있으면 문서를 열 수 있어도 `PublishIR`는 실패할 수 있다. 이때 `LastValidIR`와 그 generation은 유지된다. Slang 생성과 Scene product 검증은 [생성 계약](MaterialSlangCodegen.md), [제품 계약](MaterialGraphProduct.md)을 따른다.

## migration과 실패 복구

`LXMaterialAsset::Load`는 손상된 primary를 읽지 못하면 `.bak` 복구를 시도한다. 다만 `CompatibilityError`로 분류되는 새 schema/domain/type 문제에는 구버전 backup을 대신 읽지 않는다. `Save`는 staging과 교체를 사용하며 정상 primary를 backup으로 보존한다. 손상 primary를 수리할 때 정상 backup을 손상본으로 덮지 않는다.

문서의 일반 `Reload`와 Editor의 명시적 디스크 reload 명령은 같은 정책이라고 가정하지 않는다. Editor는 사용자가 선택한 원본을 엄격하게 확인하는 경로를 따로 갖는다. 저장 성공, IR 성공, renderer generation 게시를 각각 표시해야 한다.

## 검증과 남은 요구

`material_graph_probe.cpp`와 codegen fixture는 archive/IR/진단의 회귀 입력이다. 현재 문서는 실행 결과를 새로 선언하지 않는다. 새로운 Blender 노드를 추가할 때 registry, archive revision, 타입 검증, lowering, runtime 자원, 저장 왕복·복구 회귀를 함께 마련해야 한다. schema에 보이는 socket이나 enum만으로 지원 완료를 주장할 수 없다.

## 구현 근거

- [`Lattice/Material/LXMaterialGraph.h`](../../Lattice/Material/LXMaterialGraph.h)
- [`Lattice/Material/LXMaterialGraph.cpp`](../../Lattice/Material/LXMaterialGraph.cpp)
- [`Lattice/Material/LXMaterialIR.h`](../../Lattice/Material/LXMaterialIR.h)
- [`Lattice/Material/LXMaterialIR.cpp`](../../Lattice/Material/LXMaterialIR.cpp)
- [`Lattice/Material/LXMaterialNodes.cpp`](../../Lattice/Material/LXMaterialNodes.cpp)
