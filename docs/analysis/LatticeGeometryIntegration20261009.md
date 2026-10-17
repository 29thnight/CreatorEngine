# LX 기하 통합 기록과 현재 METEOR 연결

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 옛 설명에서 바뀐 기준

2026-10-09의 CPU LOD 선택·mesh shader 내부 frustum rejection 설명은 당시 수직 슬라이스다. 현재 SceneHost의 정본 연결은 [GpuDrivenMeshletDxrWiring](../design/GpuDrivenMeshletDxrWiring.md)이다. 현재 제품 pipeline은 METEOR Geometry, Cull.First, Occluder, Cull.Final, Surface를 명시적으로 조립하고 Shadow는 Geometry를 독립 소비한다.

geometry 단계는 current/previous/shadow pose producer를 구분한다. object-space 결과는 인스턴스 world transform과 분리되어 공유된다. visibility와 indirect argument는 recording 소유이며 다른 view/frame이 덮어쓰지 못한다. consumer는 해당 graph의 versioned read를 선언해야 한다.

## HZB와 indirect의 의미

GpuGeometryVisibility의 current-frame occlusion은 앞서 선언된 같은 view의 standard-Z depth를 받는다. tested consumer가 그 depth에 다시 쓰는 feedback 구조는 허용되는 입력이 아니다. history는 source/view/revision/completion identity를 보존한 독립 입력이다. shadow selection을 camera visible list의 재사용으로 축소하지 않는다.

MeshDispatchBin은 DispatchMeshIndirect 전용이다. DrawIndexedIndirect와 argument 단어 모양이 유사해도 혼용할 수 없다. ordered meshlet mode와 preserved instance stream은 투명 순서와 원래 입력 계약을 유지하기 위한 구분이다.

## capability 제한

일반 GpuGeometryVisibility helper가 unsupported device에서 nullable 결과를 허용하는 것과 SceneHost가 CPU/direct surface fallback을 지원하는 것은 다르다. 현재 SceneHost는 ExplicitVersioned와 meshIndirect, cooked meshlet 입력을 요구한다. helper만 읽고 제품 fallback을 약속하지 않는다.

옛 보고의 golden·validation·성능 숫자는 당시 evidence에 남긴다. 현재 소스 연결을 확인했지만 새 품질·성능·backend 행렬을 실행하지 않았다. simplification error도 인증된 형상 오차 한계와 동일시하지 않는다.

## 구현 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl](../../Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl)
- [Engine/RenderEngine/MaterialGraphFrameGeometry.h](../../Engine/RenderEngine/MaterialGraphFrameGeometry.h)
- [Engine/RenderEngine/GpuGeometryVisibility.h](../../Engine/RenderEngine/GpuGeometryVisibility.h)
- [Engine/RenderEngine/GpuGeometryOcclusion.h](../../Engine/RenderEngine/GpuGeometryOcclusion.h)
