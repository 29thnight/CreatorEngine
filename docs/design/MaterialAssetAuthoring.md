# 자산 중심 Material 편집과 게시

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 원본과 사용자의 작업 흐름

Material 편집 session은 `.asset`의 material identity와 `.shadergraph`의 graph identity를 함께 다룬다. graph의 node tree/Blackboard/output이 shader 의미의 원본이고 generated ShaderMeta/Slang은 읽기 전용 캐시 산출물이다. MeshRenderer는 재질 자산을 참조하며 개별 instance override와 자산 자체 변경을 구별한다.

창은 새 material, 기존 asset GUID, MeshRenderer에서 열 수 있다. 미완성 draft는 renderer의 accepted material을 직접 변경하지 않는다. worker가 exact document/revision snapshot을 compile하고 현재 revision과 일치하는 준비 결과만 적용 후보가 된다.

## Apply, Save, Assign

`Apply`는 준비된 draft revision과 expected accepted asset, 현재 디스크 내용을 확인한 뒤 `DataSystem::PublishMaterialAsset`로 게시한다. 같은 자산의 사용자에게 영향을 주며 디스크 영속화와는 별개다. 이름을 바꿔 기존 asset을 덮으려 하면 Save As Copy를 요구한다.

`Save`는 현재 draft가 Apply된 상태이고 accepted owner가 그대로일 때만 material+graph를 저장한다. `EditorAssetDatabase::SaveMaterialGraph`는 authoring mode, random-v4 standalone identity, accepted generation, generated Slang/metadata 일치, Assets 안의 서로 다른 `.asset`/`.shadergraph` 경로를 확인한다. unrelated asset/meta를 덮거나 identity를 바꾸지 않는다.

`Assign`은 Apply·Save된 깨끗한 asset reference를 선택 MeshRenderer에 Undo 가능한 작업으로 배치한다. 대상 scene/entity/component identity와 잠금을 재확인한다. 이 참조의 영속화는 Scene 저장이 별도로 필요하다.

자동 저장도 존재한다. `TickAutomaticSave`는 활성 gesture가 없고 현재 revision의 compile이 준비되며 변경 후 0.6초가 지난 경우 Apply 후 Save를 시도한다. 실패한 동일 revision을 매 frame 반복하지 않는다. 따라서 메뉴에 Apply/Save가 분리되어 있다는 이유로 모든 편집이 수동 적용 대기라고 설명하면 안 된다.

## 외부 변경과 복구

`CheckDisk`는 material/graph의 existence와 exact bytes를 이전 snapshot과 비교한다. 외부 수정/삭제가 있으면 Reload 또는 copy를 요구한다. `Reload`는 dirty draft를 버리려면 확인이 필요하며 saved asset identity와 referenced graph를 엄격히 읽는다. malformed primary를 오래된 backup으로 조용히 바꾸는 경로가 아니다.

Reload는 editor draft만 교체하고 accepted material을 암묵적으로 게시하지 않는다. 이후 compile/Apply가 필요하다. failed read/compile/publication은 열린 draft와 마지막 정상 accepted owner를 보존한다. document ID/revision이 바뀐 완료 작업을 새 문서에 적용하지 않는다.

## 수명과 경계

AssetDepot/GenerationStore가 exact artifact와 texture representation을 pin하고 renderer는 frame의 immutable source를 소비한다. session이 scene/object raw pointer를 장기 보관하는 방식으로 구현하면 안 된다. Scene 전환 시 target identity를 재조정하고 dirty draft의 수명과 preview 요청을 분리한다.

현재 Material 창은 모든 Blender 노드/그룹/Blackboard UI가 완성된 제품이 아니다. [편집기 계약](MaterialNodeEditor.md), [LX 잔여 계획](../plans/LatticeAdoptionPlan.md)에서 구현과 요구를 구분한다. 이번에는 실제 저장/재개방을 실행하지 않았다.

## 구현 근거

- [`Editor/EngineGUIWindow/MaterialGraphWindow.cpp`](../../Editor/EngineGUIWindow/MaterialGraphWindow.cpp)
- [`Editor/EngineEntry/EditorAssetDatabase.cpp`](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [`Engine/RenderEngine/DataSystem.h`](../../Engine/RenderEngine/DataSystem.h)
- [`Engine/RenderEngine/MaterialGraphRuntime.h`](../../Engine/RenderEngine/MaterialGraphRuntime.h)
- [`Engine/RenderEngine/AssetDepot/MaterialAssetRuntime.h`](../../Engine/RenderEngine/AssetDepot/MaterialAssetRuntime.h)
