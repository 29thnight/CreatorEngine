# SDF 텍스트의 자산·레이아웃·렌더 경로

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 실제 소비 경로

DataSystem의 font 로드와 FontAsset가 TTF/OTF를 읽고 stb_truetype로 필요한 glyph SDF를 생성한다. TextComponent::PrepareTextLayout은 게임 스레드에서 문자열·폰트·크기·폭·정렬 변경을 레이아웃으로 만든다. UIRenderProxy가 불변 layout을 전달하고 EnhancedUIPass::AppendTextRects가 glyph마다 rectangle을 만든다.

화면 UI는 EnhancedUIPass, 평면/월드 텍스트는 EnhancedSpritePass의 signedDistance 샘플링을 사용한다. atlas texture는 일반 RHI texture cache와 graph shader-read 자원으로 운반된다. UI.slang과 WorldSprite.slang은 SDF coverage를 구하고 기존 이미지 모드와 구분한다.

## 지원과 한계

UTF-8 decoder는 과잉 인코딩·surrogate·잘못된 continuation 등을 대체 문자로 처리한다. 줄바꿈, 탭, 같은 font face의 kerning, 문자 단위 줄바꿈과 좌/중앙/우 정렬이 구현되어 있다. SFNT table 범위를 확인하고 collection·압축 container는 거부한다. 이 검사를 임의 외부 폰트에 대한 보안 보증으로 확대하지 않는다.

복잡한 script shaping, ligature/BiDi, grapheme-aware 줄바꿈, color emoji, MSDF, rich text와 완전한 UI clipping 제품은 이 경로의 구현으로 주장하지 않는다. 저장된 기존 fontSize 숫자를 자동 단위 변환하지 않는다.

## 아틀라스 수명

glyph 좌표는 append-only page에 놓이고 기존 좌표를 이동하지 않는다. layout은 page owner를 보존하며 page의 현재 immutable texture snapshot을 rectangle/item으로 봉인한다. 새 glyph 추가가 이미 제출한 프레임의 픽셀을 바꿔서는 안 된다. snapshot 예산이 기존 frame/layout 때문에 소진되면 일시적 실패를 표시하고 재시도할 수 있다.

FontAsset에는 파일·face·glyph·atlas page·살아 있는 snapshot·입력/레이아웃 크기 상한이 있다. CPU snapshot byte 상한과 GPU cache의 실제 VRAM 사용은 다르다. 오래된 GPU 사본은 cache retirement를 따라야 하며 프레임 수만으로 즉시 파괴하지 않는다.

## 기본 폰트와 패키지

Resources/Fonts/Runtime의 Inter와 Nanum Gothic을 기본·fallback으로 사용한다. 정확한 원본과 라이선스는 해당 README를 따른다. 프로젝트 raw 폰트의 GUID/패키징과 source-free 자산 bootstrapping은 서로 다른 계약이며, FontAsset가 파일을 읽는다는 사실만으로 모든 배포 형식에서 프로젝트 폰트를 지원한다고 판단하지 않는다. 현재 별도의 cooked SDF atlas 형식을 전제하지 않는다.

## 검증 절차

Latin/한글과 잘못된 UTF-8, fallback 부재, 좁은 폭, 각 정렬·flip·색/알파 변경을 CPU probe와 실제 Game/Scene/World canvas에서 확인한다. atlas가 커지는 동안 오래된 프레임을 유지하고 texture owner·GPU retirement를 검사한다. source 없는 패키지에서는 기본 폰트·라이선스와 실제 project font 지원 경계를 별도로 확인한다.

Tools/regression/sdf_font_cpu_probe.cpp와 sdf_text_regression.txt는 준비된 검사 입력이다. 그 파일이 존재한다는 사실이나 ui.drawitems의 후보 수는 화면 성공·GPU 수용을 증명하지 않는다. 이번 개정에서 해당 검사를 실행하지 않았다.

## 소스 근거

- [Engine/RenderEngine/FontAsset.cpp](../../Engine/RenderEngine/FontAsset.cpp)
- [Engine/RenderEngine/FontAsset.h](../../Engine/RenderEngine/FontAsset.h)
- [Engine/RenderEngine/DataSystem.cpp](../../Engine/RenderEngine/DataSystem.cpp)
- [Engine/SceneRuntime/TextComponent.cpp](../../Engine/SceneRuntime/TextComponent.cpp)
- [Engine/RenderEngine/UIRenderProxy.h](../../Engine/RenderEngine/UIRenderProxy.h)
- [Engine/RenderEngine/Render/Passes/UI/EnhancedUIPass.cpp](../../Engine/RenderEngine/Render/Passes/UI/EnhancedUIPass.cpp)
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedSpritePass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedSpritePass.cpp)
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/UI.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/UI.slang)
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/WorldSprite.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/WorldSprite.slang)
- [Resources/Fonts/Runtime/README.md](../../Resources/Fonts/Runtime/README.md)
