# 보관: 재질 그래프 단계 이력의 해석

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 완료선의 구분

옛 MAT/LX 항목은 typed graph 문서, IR·shader 생성, runtime program/binding, Editor 저작 경험, 최종 rendered parity를 서로 다른 단계로 나누었다. 그 구분은 유지한다. 한 노드가 registry에 존재하거나 shader가 컴파일되었다고 모든 renderer route와 Blender 품질 대응이 완료된 것은 아니다.

현재 DataSystem은 authoring graph를 읽고 복구 가능한 경우 owner 경계에서 별도 default graph를 게시한다. MaterialGraphSceneHost는 program과 binding을 준비해 sealed scene 입력을 소비한다. Geometry·Surface·Shadow·Blended는 현재 METEOR graph의 각 consumer다. 옛 “공통 소비가 아직 없다”는 상태를 최신 코드에 적용하지 않는다.

## 수명과 품질

program generation과 render binding은 제출된 GPU 작업이 끝날 때까지 살아 있어야 한다. graph 수정의 새 generation은 기존 frame이 잡은 old generation을 제자리 변경하는 방식이 아니다. shader compile 성공과 device pipeline 준비 실패·pending을 구분한다.

재질 quality는 고정 scene-linear reference, texture/color-space, feature route, light/volume 조건과 실제 frame을 함께 비교해야 한다. preview 하나의 성공이나 예전 golden 숫자로 모든 특수 재질·backend·성능을 수용하지 않는다.

## 현재 정본

상태와 우선순위는 [BlenderMaterialGraphPlan](../BlenderMaterialGraphPlan.md), 제품 연결은 [GpuDrivenMeshletDxrWiring](../../design/GpuDrivenMeshletDxrWiring.md)을 따른다. 이 보관 경로는 과거 dashboard note의 누적 사본을 대신하는 호환 요약이다. 측정 JSON·이미지는 역사적 자료로 그대로 유지하며 새 빌드나 render 비교는 하지 않았다.

## 구현 근거

- [Engine/RenderEngine/DataSystem.cpp](../../../Engine/RenderEngine/DataSystem.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
