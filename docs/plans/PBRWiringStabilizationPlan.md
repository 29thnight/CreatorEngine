# PBR 입력·자산·제품 출력 배선 점검

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 현행 소유자

실제 화면 경로는 BuildLiveFramePacket→PublishLiveFrame→BuildPipelineDesc→MaterialGraphSceneHost다. SceneHost가 정확한 material program·geometry·texture owner를 준비하고 Surface에서 MRT를 복원하며 METEOR.Scene.Color에서 HDR를 발행한다. 삭제된 일반 GBuffer/Deferred pass에 옛 검사를 다시 붙이지 않는다.

재질 생성·쿠킹·프로그램 지원 범위는 material 전용 문서가 소유한다. 여기서는 source의 의미가 sealed draw와 실제 제품 픽셀까지 보존되는지 점검한다. node 등록·상수 값 기록·draw count만으로 최종 조명 응답을 검증하지 않는다.

## 입력에서 픽셀까지

base color·metallic·roughness·AO·normal·emission의 색 공간과 값 범위를 자산 적재, packed material, 실제 shader binding, surface attachment, HDR/display 순서로 추적한다. UV transform·mip·normal 공간·sampler의 계약을 중간 숫자와 픽셀 양쪽에서 확인한다. MaterialTextureTable의 단일 effective sampler 선택을 텍스처마다 독립적인 sampler 지원으로 확대하지 않는다.

공유 Material의 인스턴스 override와 material/texture/geometry generation을 각각 기록한다. prepared와 published 신원을 섞지 않고 성공한 정확한 입력만 SceneHost에서 소비한다. 예전 재질 세대나 native fallback으로 준비 실패를 숨기지 않는다.

## reload·복구 경계

DataSystem::LoadAndPublishModelAssetGeneration은 후보를 적재·검증한 뒤 Publish를 수행한다. resident ContentReload의 실패는 기존 정상 generation을 먼저 없애는 방식으로 처리하지 않는다. Editor authoring recovery는 canonical source·ID 조건에서 재저작을 요청하고 strict loader를 다시 거친다. Player cooked 경로가 source를 재생성하는 기능은 아니다. 기존 snapshot과 신규 lookup의 세대 공존·texture owner 수명을 따로 검사한다.

## 실제 사용할 진단

Editor 명령 render.pbr.capture는 RequestLivePbrCapture로 연결된다. 기본/native, controlled, motion, observed, replay의 허용 조합은 함수에서 검사하며 요청 거부·busy·timeout·완료를 구분한다. controlled는 정적 렌더 입력 재현 모드이고 전체 게임 시뮬레이션 clock을 고정하는 기능이 아니다.

EnhancedPbrCapture는 stage가 발행한 정확한 HDR handle을 읽고 capture measurement domain을 설정한다. 같은 versioned handle은 readback을 공유할 수 있지만 stage 이름의 연관은 남긴다. GPU 제출 완료 또는 안전한 abort 뒤에 readback을 해제한다. native golden과 실제 temporal 관찰은 provenance가 다르므로 서로 대체하지 않는다.

## 수용 체크리스트

동일 sealed 입력 반복은 재현성, 알려진 AO/emission/normal/sampler fixture는 의미 정확성, 독립 적분·CPU oracle은 물리/수치 정확성으로 분리한다. HDR/display 오류 주입, 잘못된 generation·손상 asset·stale 후보·동일 generation 중복 알림·명시 제거·복구·실제 source reimport를 검사한다. 장시간 상태 counter와 마지막 frame 픽셀만으로 모든 frame의 무결함을 주장하지 않는다.

과거 W0~W9의 로컬 측정·완료율은 이번 감사의 실행 증거가 아니다. 당시 장시간 자료·binary·장면·구성은 원본 재검토가 필요하다. 현행 graph-only METEOR와 temporal 이후 배선을 위 경계로 다시 수용해야 하며 Vulkan 교차 픽셀은 BackendParityPlan이 소유한다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/DataSystem.cpp](../../Engine/RenderEngine/DataSystem.cpp)
- [Engine/RenderEngine/Render/Scene/MaterialTextureTable.h](../../Engine/RenderEngine/Render/Scene/MaterialTextureTable.h)
- [Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h](../../Engine/RenderEngine/Render/Scene/EnhancedPbrCapture.h)
- [Editor/EngineEntry/Commands/RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp)
