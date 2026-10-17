# 재임포트·실제 컴포넌트·저장 씬의 종단 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 제품 연결

`assets.texture.reimportprobe`는 commandlet에 등록된 격리 회귀 진입점이다. `texture_reimport_scene_probe.inl`이 실제 EditorAssetDatabase, DataSystem, ImageComponent/SpriteRenderer, 씬 저장/로드와 일반 프레임 진행을 사용한다. marker를 가진 fixture 프로젝트를 요구하며 일반 자산에 파괴적 시험 입력을 적용하는 명령으로 사용하지 않는다.

`verify-texture-reimport-scene.ps1`은 `Build/TRS-*` 새 프로젝트에 Tiny4.png와 canonical GUID sidecar를 만들고 실제 Editor를 실행한다. 결과는 JSONL의 단일 probe terminal 결과와 passed flag, 기대 verdict를 모두 확인한다. 마지막에는 PNG와 runtime DLL hash 및 대상 소스 drift를 검사한다.

## 직렬화 버그를 막는 계약

cooked Texture description의 loose path/name은 비어 있을 수 있다. `ImageComponent::Load`는 origin GUID를 통해 DataSystem에 등록된 경로를 찾고 texturePaths에 저장한다. 유효한 cooked 참조 경로를 찾지 못하면 빈 문자열을 저장하지 않는다. `SpriteRenderer::SetSprite` 역시 같은 방식으로 m_SpritePath를 만든다. 임시 procedural texture와 null 해제는 별도의 경우다.

자동 재임포트는 기존 씬을 새로 열지 않고 description과 CodecImage가 모두 준비된 뒤 owner만 바꾼다. 교체된 pin 테이블은 이전 seal된 프레임의 table을 수정하지 않는다. 저장 참조의 복원과 CPU 이미지 4→2→4 변화는 함께 확인해야 한다. 화면 모양만 같아도 빈 경로를 저장했다면 재시작 시 실패할 수 있다.

## probe의 단정 묶음

현재 runner의 기대 verdict는 ImageComponent/SpriteRenderer, 자동 컴포넌트 갱신, Undo 유지, prefab 자동 Apply/Undo/Redo를 포함한다. 최대 크기 변경 후 자동 2×2 적용, 미저장 씬 보존, 저장 씬 재열기, 손상 cook/실패 reload의 이전 값 유지와 원본 4×4 복구가 서로 독립된 확인 지점이다. 전체 source bytes와 GUID가 끝에 복구되었는지도 중요하다.

prefab 확인은 단순 디스크 저장 여부보다 넓지만 모든 override 종류의 동등성 검증은 아니다. 동일한 prototype의 모든 컴포넌트에 대한 자동 갱신을 일반화하지 않는다. SpriteSheet/Decal/재질 소비는 코드에 연결되어 있어도 이 runner의 명시적 최종 소비자 목록과 다르다.

## 증거와 한계

2026-10-10 기록의 `Build/TextureReimportScene/result.json` 및 TRS별 results.jsonl은 당시 실행 산출물이다. 본 개정은 probe·runner·제품 호출을 다시 읽었으며 실행하지 않았다. 현 [텍스처 정본](../plans/TexturePipelinePlan.md)과 [Sprite 저장 참조](TextureSpriteReimportValidation20261010.md)가 동작 설명을 소유한다.

추가 검증은 large/cube/array 입력, 다중 재질 슬롯, hidden scene, selection 변경과 shutdown 경합, 독립 GPU pixel oracle 및 packaged Player 소비다. 정상 종료 로그나 작은 fixture의 Ready만으로 장시간 안정성과 전체 포맷 수용을 주장하지 않는다.

## 구현 근거

- [Tools/regression/texture_reimport_scene_probe.inl](../../Tools/regression/texture_reimport_scene_probe.inl)
- [Tools/regression/verify-texture-reimport-scene.ps1](../../Tools/regression/verify-texture-reimport-scene.ps1)
- [Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Engine/SceneRuntime/ImageComponent.cpp](../../Engine/SceneRuntime/ImageComponent.cpp)
- [Engine/SceneRuntime/SpriteRenderer.cpp](../../Engine/SceneRuntime/SpriteRenderer.cpp)
