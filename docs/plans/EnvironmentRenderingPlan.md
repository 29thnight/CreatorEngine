# 배경 표시와 IBL 환경의 현재·향후 계약

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 구현된 제품 경로

RenderPassSettings는 skyboxTextureName 기본 forest.ceibl과 m_isSkyboxEnabled 기본 false를 갖는다. live frame packet이 설정을 봉인하고 SkyBox 노드는 전역 배경 표시와 view-only HideSkyBox를 함께 소비한다. 배경 숨김은 IBL 자산 해제 또는 광원 차단과 같은 설정이 아니다.

EnhancedSceneRenderer의 환경 준비 작업은 선택 파일을 읽고 identity/recipe를 검증한다. raw HDR decode는 저작 경로이고 Player는 완성된 prebuilt .ceibl을 요구한다. source/recipe가 다른 cache를 정상 환경으로 재사용하지 않는다. IBL map 생성·업로드와 제출 완료 뒤 cooked capture 게시도 구분한다.

SkyBox가 배경을 숨길 때의 중립 색과 조명용 irradiance/prefiltered map은 별도 의미다. 배경 off가 직접광·방출·mesh visibility를 바꾸는 토글이 되어서는 안 된다.

## 아직 필요한 schema

SceneRenderProfile은 현재 RenderPassSettings를 담으며 완전한 Environment schema를 제공하지 않는다. 환경 asset 참조, showBackground/backgroundColor/backgroundIntensity와 diffuse/specular IBL 각각의 enabled/intensity를 분리하는 목표를 보존한다. 회전·색·세기의 linear/pre-exposure 의미, 기본값·범위, 프로젝트/씬/일시 view override 우선순위를 하나의 schema로 정의해야 한다.

## 남은 구현 순서

1. ENV-A: 현재 토글과 view override, 명시 환경 선택 성공/실패 정책을 고정한다
2. ENV-B: 제품 profile/resolver와 안정 environment 참조·저장/재로드를 연결한다
3. ENV-C: diffuse/specular/background의 독립 가중치를 모든 실제 material/light 소비자에 연결한다
4. ENV-D: cooked/cache key와 invalidation, Editor/Player package source-free 조건을 닫는다
5. ENV-E: 배경/IBL 독립 조합, 직접광·방출 보존, 선택 실패·hot reload·resize와 비용을 검증한다

## 수용 범위

배경 off+IBL on, 배경 on+IBL off와 diffuse/specular 개별 off를 같은 sealed scene으로 비교한다. invalid source/recipe가 마지막 정상 환경을 깨뜨리지 않는지도 확인한다. 기존 제품 품질 계획의 주 수용은 DX12 Debug/Release이며 Vulkan 교차 픽셀 비교는 BackendParityPlan으로 별도 기록한다. 이번 감사는 실행하지 않았고 과거 10인일 잔여 추정은 현행 재산정 없이 일정으로 확정하지 않는다.

## 소스 근거

- [Engine/RenderEngine/Interfaces/RenderPassSettings.h](../../Engine/RenderEngine/Interfaces/RenderPassSettings.h)
- [Engine/RenderEngine/SceneRenderProfile.h](../../Engine/RenderEngine/SceneRenderProfile.h)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSkyBoxPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSkyBoxPass.cpp)
- [Engine/RenderEngine/Assets/CookedEnvironment.cpp](../../Engine/RenderEngine/Assets/CookedEnvironment.cpp)
- [Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.cpp](../../Engine/RenderEngine/RHI/DX12/EnhancedIBLGenerator.cpp)
