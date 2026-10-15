# UI 좌표·수명·렌더 제출과 후속 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 호출부. 이 문서는 현재 구현을 설명하며, 이번 문서 갱신에서 빌드·테스트·제품 실행을 수행하지 않았다.

## 현재 배선

Scene은 canvas handle 목록과 UI layout을 보유한다. Canvas/UIComponent는 scene entry/exit로 멤버십을 연결하고 UITickSystem은 canvas order, text/sprite 등의 비가상 tick을 수행한다. UIManager singleton도 여전히 생성·지연 연결·입력/selection 경로에 존재한다. UISceneContext/UIInputRouter라는 미래 이름을 현재 구현된 제품 API처럼 쓰지 않는다.

UIComponent의 canvas/navigation runtime cache는 EntityHandle이다. 저장 정본은 source-relative hierarchy route이며 옛 navigation instance ID는 읽기 승격 경로로만 처리한다. DDOL 이탈에서 cache를 비우고 새 Scene에 모두 붙은 뒤 재해석한다. invalid handle이면 resolve를 실패로 처리하고 오래된 pointer를 따라가지 않는다.

## RectTransform 좌표

anchor와 pivot은 좌상단 (0,0), 우하단 (1,1)의 y-down 규약이다. layout world rect는 화면 중심을 원점으로 한 pixel 좌표를 사용하고 screen position API는 좌상단 pixel 좌표를 사용한다. anchoredPosition은 anchor reference에서 pivot까지의 local offset이며 world pivot 자체가 아니다.

각 축의 크기는 parentSize×(anchorMax-anchorMin)+sizeDelta×layoutScale이다. anchor reference는 anchorMin+(anchorMax-anchorMin)×pivot, world pivot은 parentOrigin+parentSize×reference+anchoredPosition×scale, rect min은 worldPivot-size×pivot이다. stretch와 point anchor가 같은 식을 사용한다.

최상위 screen canvas는 GetScreenRootRect/DriveAsCanvasRoot로 화면을 따르고 Canvas::ComputeScaleFactor가 자식 offset/font scale을 정한다. 기본 ScaleWithScreenSize·1920×1080이며 match는 log-space width/height 보간이다. world canvas는 sizeDelta/pivot의 local rect를 사용하고 spatial Transform은 따로 유지한다.

## invalidation과 입력

Rect setter는 MarkDirty를 게시한다. UpdateLayout은 자식 재귀를 하지 않고 Scene driver가 부모 변경을 전파한다. worldRect/layoutScale은 derived cache라 저장하지 않는다. spatial dirty와 UI dirty를 섞어 screen resize를 누락하지 않는다.

InputManager의 gameplay input gate, UIManager, UIButton은 현재 입력 경로다. multi-view pointer capture/focus/navigation authority를 새 router로 통합하는 것은 후속 설계 요구다. 기존 화면 좌표→중심 layout 좌표 변환과 render-mode별 hit test를 보존하며 screen/world canvas를 같은 사각형으로 판정하지 않는다.

## 렌더와 텍스트의 현재 구현

UIProxyBridge는 Image/Text/SpriteSheet의 값을 RenderEngine proxy data로 복사한다. RenderScene에는 `UIDrawState`/`UIDrawSnapshot` 값 복사 표면이 이미 존재한다. text proxy는 immutable TextLayout owner를 포함하고 EnhancedUIPass가 glyph 사각형을 제출한다. 따라서 “텍스트 렌더와 값 snapshot이 전혀 없다”는 옛 계획 전제는 현재에 맞지 않는다.

FontAsset은 변경된 text를 CPU에서 준비하며 render thread에서 파일 I/O/래스터화를 수행하는 API가 아니다. TextAtlasPage는 append-only glyph UV와 최신 immutable texture를 유지하고 frame seal이 실제 texture를 pin한다. 현재 제한은 font 32MiB, text 1MiB, layout 16384문자, cached glyph 4096, atlas 8 page, live snapshot 16이다. trusted project/engine font만 지원하며 arbitrary network font의 안전한 parser를 보장하지 않는다.

값 draw snapshot의 존재가 UI 전체 runtime 통합 완료를 의미하지 않는다. SpriteSheet proxy 생성에는 sidecar 로딩 경로가 남아 있으며 Canvas/UIManager/UITickSystem의 등록 경계도 함께 검토해야 한다. 실제 sorting/clipping/material/atlas 수명은 렌더 소비자까지 대조해야 한다.

## 유지할 후속 요구

- UI runtime의 scene 소유권과 전역 manager 잔여 책임을 분명히 나누고 migration 중 중복 registry를 금지한다
- pointer/keyboard/gamepad focus와 capture, Canvas membership, target graphic/navigation의 invalid handle 동작을 명시한다
- layout dirty-root propagation과 reusable layout component는 측정·회귀 뒤 확장한다
- clip rect·sort key·world/camera/overlay mode를 RHI 중립 값 계약으로 통합한다
- Editor save/reload/undo, prefab navigation remap, DDOL, component removal, multiple canvas를 기능별로 수용한다

후속 목표를 이루기 위해 이미 있는 text renderer·handle cache·snapshot을 다시 만들지 않는다. 이번 source-only 개정은 픽셀·입력·장치별 회귀를 실행하지 않았다. 세부 좌표 읽기는 [RectTransformAnalysis](../analysis/RectTransformAnalysis.html)을 참조한다.

## 구현 근거

- [Engine/SceneRuntime/RectTransformComponent.h](../../Engine/SceneRuntime/RectTransformComponent.h)
- [Engine/SceneRuntime/RectTransformComponent.cpp](../../Engine/SceneRuntime/RectTransformComponent.cpp)
- [Engine/SceneRuntime/Canvas.h](../../Engine/SceneRuntime/Canvas.h)
- [Engine/SceneRuntime/UIComponent.h](../../Engine/SceneRuntime/UIComponent.h)
- [Engine/SceneRuntime/UIManager.h](../../Engine/SceneRuntime/UIManager.h)
- [Engine/SceneRuntime/UITickSystem.h](../../Engine/SceneRuntime/UITickSystem.h)
- [Engine/SceneRuntime/UIProxyBridge.cpp](../../Engine/SceneRuntime/UIProxyBridge.cpp)
- [Engine/RenderEngine/RenderScene.h](../../Engine/RenderEngine/RenderScene.h)
- [Engine/RenderEngine/FontAsset.h](../../Engine/RenderEngine/FontAsset.h)
- [Engine/RenderEngine/Render/Passes/UI/EnhancedUIPass.h](../../Engine/RenderEngine/Render/Passes/UI/EnhancedUIPass.h)
