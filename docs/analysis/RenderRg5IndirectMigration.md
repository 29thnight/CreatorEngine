# SSAO·SSGI와 history 버전

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

SSAO·SSGI와 history 버전에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

SSAO Compute/Filter는 새 UAV 출력을 Write하고 실제 입력을 Read한다. SSGI는 HiZ·Trace·Resolve·Filter·Composite와 StoreHistory로 나뉘며 이전 history와 현재 저장 destination을 구분한다. computeCompatible와 recordingIndependent 표시는 실제 pass별로 선언한다.

## 변경·검증 기준

history ping-pong의 물리 slot 번호와 논리 read/write 역할을 혼동하지 않는다. resize·reset·abort 뒤 유효하지 않은 history를 읽지 않는지, 해당 frame의 motion/responsive 입력이 연결됐는지 확인한다. compute 선언만으로 compute queue 배치나 성능 이득을 보장하지 않는다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSAOPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSAOPass.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)

## 이전 기록

[기준 커밋의 개정 전 원문](https://github.com/29thnight/CreatorEngine/blob/493536e300e274cbf59989cdd62b20310c8ae64f/docs/analysis/RenderRg5IndirectMigration.md)에 당시 단계·세부 앵커·측정 이력이 남아 있다. 그 실행 결과를 현재 커밋의 수용으로 승계하지 않는다.
