# 화면 SSS·SSR의 최신 색 연결

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 이 문서의 역할

화면 SSS·SSR의 최신 색 연결에 관한 과거 단계별 기록을 현행 계약으로 다시 정리한다. 공통 용어·수명·실행 모델은 [RenderGraph 정본](../plans/RenderGraphDependencySchedulingPlan.md)을 따른다. 날짜가 붙은 파일명은 링크 호환을 위해 유지한다.

## 현재 구현과 호출 경로

EnhancedSSSPass는 horizontal과 vertical 결과를 별도 transient Write로 만든다. EnhancedSSRPass도 color/depth/normal/material 입력을 읽어 새 출력에 쓴다. 현재 BuildPipelineDesc는 SSS와 SSR을 LitColor modifier로 연결하지만 pass 내부의 새 physical output은 Modify가 아닌 Write다.

## 변경·검증 기준

블랙보드 slot의 modifies와 같은 GPU texture의 ReadWrite를 동일시하지 않는다. 기능 off 또는 입력 부재의 통과 경로와 on의 실제 output, resize 뒤 descriptor를 확인한다.

과거 로컬 Build/Artifacts 경로의 수치와 완료·기성 표시는 이번 소스 감사에서 재검증하지 않았으므로 현재 통과 결과로 옮기지 않는다. 제품 기본값, 특정 fixture 성공, 여러 backend의 제품 수용은 각각 판정한다.

## 소스 근거

- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSSPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSSPass.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSRPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSRPass.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
