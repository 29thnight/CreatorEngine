# 모델 배치·지오메트리·텍스처 준비의 현재 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 책임을 나누는 기준

모델 원본 저작, immutable model generation, typed scene inputs, 씬 entity 생성, CPU image, GPU upload는 같은 객체의 수명으로 묶이지 않는다. 이 문서는 과거 G1~G9 제안을 현재 코드와 대조해 이미 존재하는 경계와 추가 요구를 구분한다. 텍스처의 cook/descriptor/image 세부사항은 [텍스처 정본](TexturePipelinePlan.md), 모델 cutover 이력은 [기존 모델 계획](archive/ModelAssetBigBangCutoverPlan.md)을 참조한다.

## 1. Editor 모델 드롭의 데이터 흐름

`Editor::ModelPlacement::Execute`는 scene ID, 모델 path, optional position을 받는다. 요청은 Scene*/Entity*를 장기 보관하지 않으며 EditorMain이 초기화·Tick·Shutdown을 소유한다. `PrepareModelAssetByPath`의 ticket을 worker가 준비하고 typed ModelSceneAssetInputs 또는 호환 ModelAssetGeneration에서 `PendingInstance::Prepare`를 만든다. typed inputs는 같은 resolver snapshot에서 준비된 children을 전달하며 이 Prepare 자체는 source 로드·shader compile·image decode·scene mutation을 하지 않는 계약이다.

게임 스레드 Tick은 active scene ID, Play 상태, scene loading 여부를 재확인한다. 바뀌면 취소하고 이미 만든 root를 정리한다. 아직 완료되지 않은 worker를 정상 프레임에서 기다리지 않는다. scheduler가 결과 없이 종료한 경우도 실패로 수렴한다. publication이 invalidated되면 이미 root를 만들거나 세대를 게시한 뒤에는 재시도하지 않으며, 제한된 preparation retry 후 terminal 실패를 낸다.

준비 성공 뒤 `PublishPreparedModel`을 호출하고 PendingInstance를 점진적으로 Advance한다. 전체 Tick 예산은 약 2ms를 목표로 하고 프레임당 한 instance를 진행한다. Advance는 기본 16 steps, renderer activation 2개 제한을 받는다. 이 값은 협력적 작업 분할 정책이며 모든 단일 함수가 2ms 안에 끝난다는 hard real-time 보장은 아니다.

## 2. 취소·Undo·종료

배치 명령의 Undo는 요청 Cancel 경로를 사용한다. 취소된 root는 원래 scene ID를 다시 해석해 제거하고 EndFramePass로 pending destroy를 처리해 Play snapshot으로 섞이지 않게 한다. 요청 및 worker capture는 완료까지 유지되며 visible list에서 사라졌다고 모든 준비 owner가 해제된 것은 아니다.

Shutdown은 신규 body를 해제하고 accepted preparation을 정리한 후 scenes/DataSystem이 살아 있는 동안 마지막 정리를 수행한다. 진행 UI의 totalSteps=0은 준비 중 아직 측정 분모가 없다는 뜻이다. oldest active 요청의 steps를 전체 모델 byte 진행률이나 GPU 업로드 진행률로 읽지 않는다.

## 3. Vertex와 지오메트리 계약

`ModelVertexLayout.h`가 Position/Normal/Uv0/Uv1/Tangent/Color/BoneIndices/BoneWeights의 semantic, 자체 VertexFormat, permutation axis를 기술한다. offsets와 stride는 mask에서 유도된다. import 계층은 RHI 타입에 직접 의존하지 않고 render 경계에서 변환한다. baked layout hash/mask/stride와 shader input이 함께 맞아야 하며 단순 sizeof 일치만으로 소비 계약을 입증하지 않는다.

ModelSceneInstantiation은 model-local bone 이름 해석과 parent-before-child 계약을 사용한다. 잘못된 generation은 부분적인 씬 생성을 피하도록 검증한다. typed geometry/scene inputs와 호환 aggregate 경로가 함께 보인다는 이유로 모든 primitive·terrain·procedural geometry가 하나의 schema로 통합됐다고 주장하지 않는다.

## 4. 이미 구현된 옛 제안

과거 G2의 basename texture key 문제는 현재 논리 자산 key와 content image key의 분리로 읽어야 한다. G3의 immutable CPU image/중복 준비는 TextureAssetRuntime의 description/image cache와 in-flight consumer 상태에 구현되어 있다. G9의 source decode/BC cook 이동은 Editor/offline translation unit과 CECT 런타임 reader로 구현되어 있다. 이를 계속 “미착수”로 쓰면 현재 source와 충돌한다.

G4의 중립 CPU upload view는 TextureImageView와 frame pins에 존재하지만 RHI 전체가 자산 의존에서 완전히 해방되었다는 증거는 아니다. G6의 pending/ready/failed/stale/cancelled 상태와 GPU fence는 서로 다른 층이다. G7 공통 준비 추출과 G5 전체 geometry identity 보존은 각 소비자·키·range까지 별도 감사를 해야 한다.

## 5. 남은 요구와 검증

큰 모델/다중 드롭·작은 worker pool·cancel/Undo/Redo·scene 전환·Play·shutdown과 source 재임포트의 조합을 `verify-model-async-placement.ps1` 및 model generation/atomicity 검사에서 확인한다. preparation time, 최대 owner 적용 시간, 전체 완료 시간, CPU/GPU 준비 상태를 분리한다. watcher가 만든 새 material 때문에 준비 ticket이 무효화되는 사례도 포함해야 한다.

메시 API의 불필요 타입 제거, 모든 geometry 종류의 충돌 없는 key, backend-ready/부재/오류의 일관된 진단, 공통 frame resource 준비와 대규모 성능 측정은 여전히 검토할 가치가 있다. 기존 계획의 제안을 구현 완료로 간주하거나 이번 문서 변경에 과거 실측 이득을 귀속하지 않는다. 이 checkout에서 실행·빌드 없이 source wiring만 대조했다.

## 구현 근거

- [Editor/EngineEntry/EditorModelPlacement.h](../../Editor/EngineEntry/EditorModelPlacement.h)
- [Editor/EngineEntry/EditorModelPlacement.cpp](../../Editor/EngineEntry/EditorModelPlacement.cpp)
- [Engine/SceneRuntime/ModelSceneInstantiation.h](../../Engine/SceneRuntime/ModelSceneInstantiation.h)
- [Engine/SceneRuntime/ModelSceneInstantiation.cpp](../../Engine/SceneRuntime/ModelSceneInstantiation.cpp)
- [Engine/RenderEngine/Assets/ModelVertexLayout.h](../../Engine/RenderEngine/Assets/ModelVertexLayout.h)
- [Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h](../../Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h)
- [Engine/RenderEngine/TextureImage.h](../../Engine/RenderEngine/TextureImage.h)
- [Tools/regression/verify-model-async-placement.ps1](../../Tools/regression/verify-model-async-placement.ps1)
