# SDF 텍스트 경로

2026-10-08, 기준 master `3afe1daaee7b75f644ac10b12d96fb684a0e74c8`.

이 변경은 [PHASE 16 트랙 T](../plans/UISystemRedesignPlan.md)의 소스 연결이다.
빌드, 셰이더 컴파일, 엔진 실행, 테스트 실행, 화면·성능 검증은 **실행하지 않았다**.
아래 구현 목록은 런타임 수용 완료를 뜻하지 않는다.

## 연결한 경로

1. `DataSystem::LoadFontShared`가 등록된 TTF/OTF 또는 배포된 기본 폰트를 읽는다.
   자산 GUID와 정규화된 전체 경로로 캐시하며 같은 파일명을 합치지 않는다.
   기존 번들의 `ManagedAssetType::SpriteFont = 3`은 보존한다. 구 DirectXTK
   `.spritefont` 바이너리를 SDF 폰트로 잘못 읽지 않고 오류로 보고한다.
2. `FontAsset`가 기존 `stb` 의존성의 `stb_truetype`으로 필요한 글리프만 SDF로 만든다.
   별도 폰트 라이브러리, MSDF 생성기, 스레드 풀은 추가하지 않는다.
3. `TextComponent`가 GT에서 문자열·폰트·픽셀 크기·폭·정렬이 바뀌었을 때 레이아웃을 만든다.
   기존 프록시 dirty 경계로 전달하며 색·알파만 바뀌면 재래스터화하지 않는다.
   생성과 갱신 양쪽에 `fontSize * layoutScale`, flip, 활성 상태, Canvas 정보를 보존한다.
4. `EnhancedUIPass::AppendTextRects`가 하나의 텍스트를 N개 글리프 사각형으로 펼친다.
   Game View와 Player Overlay는 `EnhancedUIPass`, Editor Scene View 미리보기와
   Camera/World Canvas는 기존 `EnhancedSpritePass`의 평면 배치를 사용한다.
   두 경로가 같은 앵커·정렬·flip 계산을 공유한다.
5. 기존 중립 `Texture` → DX12/Vulkan texture cache → RenderGraph 경로로 업로드한다.
   업로드는 렌더 패스 진입 전에 이루어지고, 아틀라스는 명시적 shader-read 자원으로 선언된다.
   `UI.slang`과 `WorldSprite.slang`은 SDF의 red 채널을 읽어 derivative 기반 coverage를
   계산하고 straight alpha로 합성한다. 일반 이미지 샘플링은 유지한다.

## 글리프와 수명

- UTF-8의 과잉 인코딩, surrogate, 잘못된 continuation, 범위 밖 scalar는 대체 문자로 처리한다
- CRLF/LF/CR, 탭, 같은 face 안의 커닝, 문자 단위 자동 줄바꿈, Left/Center/Right를 지원한다
- 가로 정렬은 각 줄을 같은 layout 폭 안에 놓고, 블록의 세로 앵커는 기존처럼 중앙이다
- 새 컴포넌트의 기본 크기는 32픽셀이다. 기존에 저장한 숫자는 자동 변환하지 않는다
- `RelativePosition`은 Canvas 배율이 적용된 레이아웃 오프셋이다
- 비정상적인 숫자는 레이아웃/그리기 경계에서 걸러 GPU instance에 전달하지 않는다

아틀라스 페이지의 글리프 좌표는 추가 후 이동하거나 덮어쓰지 않는다. 불변 `TextLayout`은
이 페이지의 안정된 소유자를 참조하며, 페이지는 새 **불변 Texture**를 원자적으로 게시한다.
화면/평면 사각형으로 밀봉하는 시점에 실제 Texture를 잡고 Rect/Item/Batch가 보존한다.
따라서 새 문자열의 글리프 추가가 이전 프레임의 픽셀을 바꾸지 않으며, 오래된 정적 라벨
하나마다 4 MiB짜리 옛 아틀라스가 영구히 남는 구조도 피한다.

폰트 파일당 32 MiB, primary+fallback 2 face, 캐시 4,096 glyph, 1,024² 페이지 8장,
살아 있는 RGBA8 스냅샷 16장, 입력 1 MiB/레이아웃 16,384 scalar의 상한을 둔다.
RGBA8은 기존 RHI 포맷을 그대로 사용하기 위한 선택이다. SDF는 red 채널만 필요하지만
새 R8 포맷을 모든 백엔드에 추가하지 않았으므로 스냅샷 상한은 64 MiB다.
R8 CPU staging 최대 8 MiB와 게시 중 임시 변환 메모리는 별도다.
이 값은 CPU Texture 스냅샷 상한이다. GPU 사본은 기존 texture cache의 미사용 프레임·
메모리 압박 은퇴 정책을 따르므로, 이전 버전이 GPU에 남는 동안 총 VRAM은 이보다 클 수 있다.
한계를 넘으면 이미 준비한 대체 글리프를 사용하거나 문자열을 잘라내고 진단에 표시한다.
프레임이 옛 아틀라스를 붙든 일시적 압박은 별도로 표시하고, 여유가 생기면 같은 문자열도
GT에서 재시도한다. 영구적인 글리프/문자 수 상한은 매 프레임 재시도하지 않는다.

## 폰트와 패키지

- 기본 Latin은 저장소에 있던 Inter Regular를 그대로 재사용한다
- 한글은 공식 Google Fonts에서 고정한 Nanum Gothic Regular를 fallback으로 쓴다
- 두 폰트는 변환·서브셋 생성 없이 원본 바이트를 사용한다
- [폰트 출처·라이선스](../../Resources/Fonts/Runtime/README.md)와 SHA-256/Git blob 신원을 함께 저장한다
- Editor/Player 출력과 게임 패키지에 `Resources/Fonts/Runtime` 전체를 배포하고 해시 manifest에 포함한다
- 프로젝트 폰트는 `.meta` GUID가 필요한 raw source 자산이다. cooker가 씬·프리팹·override·번들 참조를
  등록된 폰트 GUID로 바꾸며, 미해결/다른 자산 타입/Assets 밖의 참조는 실패시킨다
- 기본 폰트는 고정 engine resource 경로로 해석하므로 현재 작업 폴더나 개발 PC의 시스템 폰트에 의존하지 않는다

최신 `PrebuiltAssetSets`의 source-free bootstrap은 프로젝트 raw `.ttf`/`.otf`를 허용하지 않고,
AssetSet에도 Font kind가 아직 없다. 따라서 프로젝트 폰트 패키징은 기존 프로젝트 소스 포함
경로에 한정한다. source-free bootstrap의 프로젝트 폰트 참조는 허용 범위를 넓히지 않고
cooker/입력 경계에서 실패시킨다. 번들 Inter/Nanum runtime resource 배포는 그대로 지원한다.

이 단계에서 atlas/metrics를 디스크에 미리 굽는 별도 cooked-font 형식은 만들지 않았다.
Player는 패키지의 원본 TTF/OTF를 읽고 첫 사용 시 직렬로 SDF를 만든다.
`stb_truetype`는 보안 경계용 폰트 파서가 아니다. 파일/테이블 범위 검사는 포함하지만,
임의의 신뢰할 수 없는 네트워크 폰트를 안전하게 처리한다고 주장하지 않는다.

## 진단과 아직 실행하지 않은 수용

`ui.drawitems`는 RT 프록시 자료를 잠금 안에서 값으로 복사한 뒤 읽는다. 명령이 씬을 tick하거나
프록시 큐를 소비하지 않는다. Text 수, 유효 glyph Rect 수와 Canvas 모드, 누락/빈 레이아웃,
대체 glyph·잘못된 UTF-8·예산 초과를 보고한다. 이것은 **CPU 그리기 후보**이며 GPU 제출/화면 성공의 증거가 아니다.

준비한 회귀 자료:

- [CLI 저작 시나리오](../../Tools/regression/sdf_text_regression.txt): Latin/한글/좁은 폭/세 정렬,
  색·문자열·크기 변경, Play/Stop 뒤 `ui.drawitems`; 마지막 씬을 열어 두어 화면을 비교한다
- 기존 paired DX12/Vulkan `vk.ui` 소스: 합성 SDF contour/반투명 색/일반 이미지의 혼합,
  위치·flip·잘못된 값·CPU owner 수명 단정
- CPU 폰트 probe 소스: Unicode/커닝/줄바꿈/정렬/한글, append-only 페이지와 이전 Texture 보존
- BuildTool/cooker probe 소스: 폰트·라이선스 누락, runtime 복사, GUID/경로/override 변환과 거부

아직 필요한 실행:

1. Windows Editor/Player Debug·Release 빌드, reflgen 생성, 두 Slang target 컴파일
2. CPU/cooker/BuildTool probe와 paired DX12/Vulkan UI 진단 실행
3. Game/Scene View의 Latin·한글, Camera/World Canvas·깊이 가림·mixed Image/Text 순서 확인
4. resize/CanvasScaler/폰트 크기 12·24·48·96, 줄바꿈·색·알파·정렬·flip 화면 비교
5. 텍스트·Canvas enable/disable, 재부모화, 씬 왕복, Play/Stop, 폰트 reload·실패 복구
6. 16개를 넘는 서로 다른 라벨, frames-in-flight, atlas/GPU cache 은퇴와 메모리 상한 확인
7. 소스 checkout이 없는 패키지에서 기본/프로젝트 폰트와 라이선스 포함, DX12/Vulkan Player 실행

아직 포함하지 않는 것: MSDF 품질 비교, shaping/ligature/BiDi, grapheme 단위 줄바꿈,
컬러 emoji, 세로 텍스트, rich text, 말줄임·부모 마스크/height clipping, PHASE 16의 나머지 위젯·UI 소유권 재설계.
