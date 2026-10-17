# MAT-7 생성 ShaderMeta 어댑터의 현재 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 이 기록의 위치

초기 MAT-7은 renderer-neutral LX program을 엔진의 검증된 ShaderMeta 계약으로 바꾸는 경계를 만들었다. 현재는 [제품 계약](../design/MaterialGraphProduct.md)과 [Scene cook](../design/MaterialGraphSceneCook.md)에 통합되어 있다. 과거의 “생성 쌍만 있고 cooked artifact에는 미포함” 상태와 LXMC v3를 현재 제한으로 남기지 않는다. 현재 Graph cooked version은 12다.

## 생성과 게시

`PublishMaterialShaderMeta`는 `LXMaterialProgram`, Graph GUID, 실제 host source, engine이 소유한 pass description과 reflection을 받는다. LX 자체가 renderer pass를 정의하지 않는다. 생성물은 실제 ShaderMeta schema를 따르며 파일 확장자만 바꾼 LX JSON이 아니다.

stable Blackboard parameter ID, typed default, label/exposed, semantic/colorSpace와 실제 물리 선언 이름을 연결한다. Texture2D는 같은 source ID가 색 공간별 resource로 여러 번 나타날 수 있다. numeric packing은 공통 `MaterialPropertyPacker`를 사용하며 Color를 두 번 선형화하지 않는다.

후보의 schema/source digest/reflection/layout을 검증한 뒤 immutable generation directory를 rename한다. 같은 identity의 기존 파일 내용이 달라졌다면 덮어써 정상 세대를 훼손하지 않는다. 실패는 accepted 파일과 caller 결과를 보존한다.

## 복구와 소유권

`GeneratedMaterialShader`는 공통 `ShaderGeneration`에서 파생하고 검증된 cooked binary contract도 보유한다. public converting owner가 파생 allocation을 유지한다. embedded metadata/layout pointer는 owner보다 오래 살 수 없는 borrow다.

`RestoreMaterialShaderMeta`는 verified product layout과 canonical pair를 받아 filesystem/compiler 없이 공통 계약을 복원한다. 복구 가능성은 단순 text parse 성공이 아니라 전체 target·layout·identity 검증에 달려 있다. 현재 Player 복구에 authoring text parser나 런타임 source compiler를 다시 끼우지 않는다.

## 검증과 남은 범위

`material_shadermeta_probe`는 typed property, alias, sampler, digest/schema 오류와 결과 보존을 검사하는 fixture다. 예전 56/68 check 기록은 각 단계의 역사적 실행이며 현재의 GPU 이미지·Editor UI·package 수용 결과가 아니다.

새 노드/property 도입에는 generated metadata, reflection, packer, source-free 복구, 이전 artifact 거부가 함께 필요하다. compiler 지원과 live Scene route 지원을 각각 확인한다. 이번 문서 갱신은 이 경계의 정적 확인이며 도구를 실행하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphShaderMeta.h`](../../Engine/RenderEngine/MaterialGraphShaderMeta.h)
- [`Engine/RenderEngine/MaterialGraphShaderMeta.cpp`](../../Engine/RenderEngine/MaterialGraphShaderMeta.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphProduct.h`](../../Engine/RenderEngine/MaterialGraphProduct.h)
- [`Engine/RenderEngine/MaterialPropertyPacker.cpp`](../../Engine/RenderEngine/MaterialPropertyPacker.cpp)
- [`Tools/regression/material_shadermeta_probe.cpp`](../../Tools/regression/material_shadermeta_probe.cpp)
