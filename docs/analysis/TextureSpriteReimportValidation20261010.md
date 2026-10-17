# SpriteRenderer 텍스처 owner와 저장 참조

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## Sprite의 두 종류의 상태

SpriteRenderer에는 현재 표시할 Texture shared owner와 저장할 m_SpritePath가 함께 필요하다. owner는 그 세대의 실제 description을 유지하며 경로는 다음 씬 로드에서 논리 자산을 다시 찾기 위한 참조다. 이미지의 basename이나 비어 있는 cooked path를 경로로 대신하면 현재 화면은 정상이어도 재열기에서 텍스처가 사라진다.

`SetSprite`는 null이면 참조를 해제하고, cooked origin이 있으면 그 GUID로 등록 경로를 조회한다. 필요한 경로를 찾지 못하면 새 owner로 바꾸기 전에 거부한다. loose 또는 origin 없는 임시 텍스처의 처리는 이와 구별된다. description이나 immutable artifact를 수정해 경로를 끼워 넣는 방식이 아니다.

## 자동 재임포트와 reload

EditorAssetDatabase는 현재 sprite owner를 수집하고 새 descriptor와 CodecImage를 준비한다. 교체 시점에 여전히 이전 owner를 가진 SpriteRenderer만 SetSprite(replacement)로 바꾼다. 이 경로는 ImageComponent의 목록 갱신과 API 모양은 다르지만 원본 세대를 pin하고 저장 참조를 보존한다는 요구가 같다.

[종단 probe](TextureReimportSceneValidation20261010.md)는 UITexture와 일반 Texture 소비를 실제 ImageComponent/SpriteRenderer로 나누어 준비하고 같은 씬에서 저장·재로드를 확인한다. `verify-texture-reimport-scene.ps1`은 최종 verdict에 두 소비자 이름이 있는지 검사한다. SpriteRenderer가 포함된 최신 검사인지 파일 이름만 보고 판단하지 않는다.

## 역사적 실패와 현재 판정

2026-10-10에는 m_SpritePath가 빈 문자열로 저장되어 reload 이후 generation이 복원되지 않는 사례가 기록되었다. 이 문서는 그 진단의 호환 요약이며 당시 `Build/TRS-Debug-360796b3`와 baseline 로그는 로컬 실행 흔적이다. 현재 SetSprite의 방어와 runner 연결을 확인했지만 그 실행을 새로 반복하지 않았다.

스프라이트 회전·크기·색상·애니메이션 프레임의 보존, SpriteSheet의 별도 동작, 여러 씬과 deletion race, GPU 출력 비교는 추가 수용 항목이다. 기본 설명과 실패 처리의 정본은 [텍스처 파이프라인](../plans/TexturePipelinePlan.md)이다.

## 구현 근거

- [Engine/SceneRuntime/SpriteRenderer.cpp](../../Engine/SceneRuntime/SpriteRenderer.cpp)
- [Engine/SceneRuntime/ImageComponent.cpp](../../Engine/SceneRuntime/ImageComponent.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Tools/regression/texture_reimport_scene_probe.inl](../../Tools/regression/texture_reimport_scene_probe.inl)
- [Tools/regression/verify-texture-reimport-scene.ps1](../../Tools/regression/verify-texture-reimport-scene.ps1)
