# Refraction·Subsurface·Volume 자원 소유

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

Refraction·Subsurface·Volume 자원 소유에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

Refraction과 Subsurface의 DeclareCaptureOutputs는 입력 맵의 Write 버전을 만든다. Refraction background color/depth와 bake samples, Subsurface reflection/irradiance는 각 생산자 출력이다. Volume은 coefficient와 합성 출력을 구분하고 SceneHost는 공간적으로 변하는 volume coefficient를 unsupported로 거부한다.

## 변경·검증 기준

Lookup의 최신 GraphInputs와 실제 lighting/depth/environment를 소비해야 한다. 반복 capture·여러 매질·같은 texture 공유에서 중복 usage를 확인한다. 화면 전체 SSS pass와 재질 subsurface cache는 별개이며 하나의 성공을 다른 쪽 수용으로 쓰지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/MaterialGraphSceneRefraction.cpp](../../Engine/RenderEngine/MaterialGraphSceneRefraction.cpp)
- [Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp](../../Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp)
- [Engine/RenderEngine/MaterialGraphSceneVolume.cpp](../../Engine/RenderEngine/MaterialGraphSceneVolume.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg5SpecialMigration.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
