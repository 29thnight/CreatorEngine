# 단일 CQB 데모의 제품 요구사항

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 아직 제작 완료가 아닌 목표

데모 의도는 한 레벨에서 약 3분의 CQB FPS 플레이와 기술 관찰을 함께 제공하는 것이다. 방 4~6개, 약 20m 이하의 동선, 재질 약 12종을 초기 범위로 삼는다. 이동·조준·사격·피격·엄폐·퇴출을 같은 공간에서 보여 주고 콘텐츠 확장보다 안정적인 한 수직 슬라이스를 우선한다. 이 수치는 구현된 level의 측정값이 아니다.

기술 모드는 pass/중간 buffer 관찰, CPU/GPU profile, 관리 스크립트 수정·reload를 의도한다. 각각의 기능이 존재하는 것과 데모 내에서 안전하게 toggle되는 제품 경험은 별도다. 실행 중 backend 교체는 현재 presentation/renderer 초기화 계약이 제공하는 API가 아니므로 데모 요구에서 제외한다. 초기 영상 기준은 DX12로 둔다.

## 현재 의존성 다시 읽기

옛 계획의 “폰트 없음”, “FMOD 전환 예정”, “모든 모델은 옛 raster 경로”는 현행 상태 설명으로 사용할 수 없다. FontAsset과 MiniaudioBackend가 존재하고 Scene renderer는 METEOR graph 경로를 조립한다. 반대로 파일 존재만으로 HUD 가독성·장치 audio·게임 조작·품질 수용까지 완료했다고 보지 않는다.

렌더링 소비는 Geometry→Cull.First→Occluder→Cull.Final→Surface를 중심으로 실제 graph와 asset closure를 따라야 한다. 데모 material/mesh는 cooked meshlet admission을 만족해야 한다. 측정 UI는 별도 ProfilerViewer와 command surface의 현재 capability를 사용하며 과거 overlay 그림을 구현으로 가정하지 않는다.

## 착수·완료 조건

착수 전에는 고정 입력·scene·필수 asset·실행 build를 선정하고 조작, 총성/발소리, UI, lighting, reload의 최소 동작을 확인한다. 모든 저장소 계획을 닫아야 한다는 일괄 선행 조건 대신 데모가 실제로 소비하는 계약을 확인한다.

완료는 package-game 결과에서 시작 scene, cooked closure, native Present, 종료, 스크립트 lifecycle과 의도한 3분 플레이를 재현하는 것이다. 기능 표·dashboard 비율·과거 남은 항목 수는 완성 근거가 아니다. 성능 예산과 품질 기준은 대표 장치·해상도·장면을 정한 뒤 명시해야 하며 현재 문서에서 납기나 FPS를 새로 약속하지 않는다.

## 구현 근거

- [Player/PlayerMain.cpp](../../Player/PlayerMain.cpp)
- [Engine/RenderEngine/FontAsset.cpp](../../Engine/RenderEngine/FontAsset.cpp)
- [Engine/SceneRuntime/Audio/MiniaudioBackend.cpp](../../Engine/SceneRuntime/Audio/MiniaudioBackend.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [BuildTool/PlayerVerification.cs](../../BuildTool/PlayerVerification.cs)
