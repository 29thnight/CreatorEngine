# W1 아이콘 선정과 비동기 썸네일 표시 계약

2026-09-11. 기준: HEAD `c46b4fdc`와 조사 시점 작업 트리, 사용자가 제공한 `editor.webp`.
이 문서는 선정 당시의 비교 근거와 비동기 썸네일 설계다. 이후 W1 제품 적용·검증은
[EditorMaterialSymbolsW1Validation.md](EditorMaterialSymbolsW1Validation.md)를 따른다.
정본 범위는 [EditorWorkspaceRedesignPlan.md](../plans/EditorWorkspaceRedesignPlan.md)를 따른다.

## 확정 방향 — 2026-09-11 사용자 결정

1. **Live Code 수동 버튼은 CoreCLR 자동 변경 감지 방향에 따라 제거 대상으로 둔다.**
   대체 아이콘을 선정하거나 수동 컴파일 기능을 새로 연결하지 않는다.
   현재 버튼은 C++ 핫리로드 은퇴 뒤 남은 비활성 자리표시자다. 자동 감지 구현의 완료를 뜻하지 않는다.
2. **공통 조작·패널·유형 아이콘 폰트는 Google Material Symbols로 결정한다.**
   Outlined를 적용 시작 스타일로 삼는다. FA·Tabler·Codicons 비교는 선택 근거로 보존하며,
   여러 폰트를 기본 UI에 혼합하는 방안은 채택하지 않는다. 본문 Inter는 별도 역할이다.
3. **Content Browser의 메시·텍스처 등은 유형 아이콘을 먼저 그리고, 비동기 썸네일이 준비되면 교체한다.**
   파일 읽기·디코딩·생성 완료를 UI에서 기다리지 않는다. GPU에서 표시 가능한 결과가 게시되면
   다음 UI 프레임부터 바로 적용하며, 실패·미지원일 때도 타일은 유형 아이콘으로 사용할 수 있다.

이 결정은 W1의 폰트·아이콘 적용 기준과 W7의 Browser 썸네일 구현 계약에 반영한다.
이후 W1에서 Material Symbols와 Browser 유형 아이콘을 적용하고 Live Code 버튼을 제거했다.
비동기 썸네일 생성·캐시·전환은 W7에 남아 있다. 아래 교체 전 코드·비교 그림은 선정 근거로 보존한다.

S&Box 공식 [Editor Tools 문서](https://sbox.game/dev/doc/editor/editor-tools/)는
도구의 `Icon("rocket_launch")`를 Google Material Icons 이름으로 지정한다.
따라서 Material 계열을 비교 기준으로 삼을 근거가 있다. 다만 첨부 스크린샷의 모든 아이콘이
같은 폰트라는 뜻은 아니며, 이미지의 실제 파일 형식은 화면만으로 판정할 수 없다.

Google은 기존 Material Icons를 갱신하지 않는 클래식 세트로, Material Symbols를 현재 세트로 구분한다.
이번 도입 대상은 Symbols다. [공식 저장소](https://github.com/google/material-design-icons)

## 첨부 화면의 역할별 분류

아래는 스크린샷의 표현과 사용자 결정을 Creator Engine에 적용할 때의 분류다.

| 위치·역할 | 사용할 표현 | 적용 기준 |
|---|---|---|
| 메뉴, 탭, 검색, 뒤로/앞으로, 새로 만들기, 정렬, 필터, 더보기 | 아이콘 폰트 | 단색 기호. 글자와 같은 배율·상태 색 적용 |
| Play/Pause/Stop/Eject, 선택/이동/회전/스케일 도구 버튼 | 아이콘 폰트 우선 | Play는 삼각형처럼 관습적인 모양 유지. 전용 3D 변환 표식이 필요하면 작은 벡터로 보완 |
| Hierarchy/Inspector/Console/Asset Browser 탭 | 아이콘 폰트 | 트리·조절기·터미널·폴더 등 기능이 드러나는 기호 |
| Hierarchy 행의 작은 파란 유형 표식 | 단색 폰트 + 상태 색, 또는 전용 벡터 배지 | 에셋 실물 사진을 모든 행에 넣지 않는다. 객체 종류와 Prefab 연결 상태를 구분 |
| Inspector 위쪽의 색 있는 객체/Prefab 표식 | 전용 유형 이미지 또는 썸네일 | 같은 대상이 Browser·Hierarchy·Inspector에서 같은 유형 기호를 공유 |
| Transform/Renderer 등 컴포넌트 제목 옆 기호 | 아이콘 폰트 또는 소수 전용 벡터 | 카메라·광원·오디오·스크립트는 일반 기호로 충분. Collider·Avatar Mask 등은 전용 기호가 더 정확할 수 있음 |
| 하단 Browser의 나무·그루터기·표지판 타일 | 유형 아이콘 → 실제 모델/Prefab 썸네일 | 비동기 생성·업로드 중에는 기호 유지. 준비 완료 시 같은 타일에 실물 미리보기 적용. 작은 유형 배지 유지 |
| Texture/Material/HDR 타일과 Inspector 에셋 참조 | 유형 아이콘 → 실제 이미지/렌더 미리보기 | 텍스처 내용, 재질 구, HDR 환경 등. 로딩 대기·진행·실패에는 기본 기호. 준비된 캐시는 즉시 사용 |
| 왼쪽 위 엔진 마크, 프로젝트 게임 그림, 플러그인 마크 | 정식 로고 이미지 | 범용 폰트 아이콘으로 브랜드를 흉내 내지 않음 |
| 월드 안 Camera/Light 표식 | 전용 텍스처, 원본은 벡터 가능 | 현재 월드 GizmoIconPass 경로와 연결. UI 글꼴 그리기와 구분 |
| XYZ 이동 축, 회전 링, 우측 상단 방향 축 | 기존 기하/전용 draw | 방향·선택·깊이에 반응하므로 정적 폰트나 한 장의 이미지로 대체하지 않음 |

이미지 사용은 PNG만을 뜻하지 않는다. 선명한 유형 기호는 SVG 원본을 보관하고 런타임이 지원하는
텍스처로 변환할 수 있다. 실제 에셋 미리보기는 해당 자산에서 생성한 이미지다.
S&Box의 고유 로고나 에셋 그림을 잘라 제품 자원으로 재사용하는 설계는 포함하지 않는다.

## 선정 당시 코드 기준선 — W1 교체 전

- `EditorFontResources::merge_icon_font()`는 `fa.h`의 폰트 한 개를 병합한다.
  블롭을 ImGui의 압축 형식으로 풀어 name table을 읽은 결과
  **Font Awesome 6 Free Solid 6.4.2**, 원본 TTF 394,668바이트였다.
  FA4 헤더는 현재 추적 파일에 없으며, 과거의 FA4/FA6 혼용 문제와 이번 의미 선정 문제는 별개다.
- `EditorWindowNames.h`의 Scene은 `users-viewfinder`, Hierarchy는 `bars-staggered`,
  Inspector는 `circle-info`다. 셋 모두 표시할 수 있는 글리프지만 역할과의 대응을 개선할 여지가 있다.
- `SceneViewWindow.cpp`의 Scale은 `group-arrows-rotate`, Orthographic은 `eye-low-vision`이다.
  전자는 사람들 주위의 회전, 후자는 저시력 표식이라 해당 조작을 설명하지 못한다.
- Inspector·Animator·에셋 선택 검색에는 `marker`, Controller 상세 팝업에는 `chess-rook`를 사용한다.
  검색/설정 기호로 바꾸는 편이 명확하다.
- Live Code의 `cubes-stacked` 버튼은 `BeginDisabled(true)` 안에서 그려지며 실행 동작이 없다.
  CoreCLR 자동 감지 방향에 따라 버튼 제거 대상으로 분류하고 교체표에서는 제외했다.
- Hierarchy는 Prefab 여부에 따라 `box-open`/`cube` 두 종류를 고른다.
  카메라·광원·오디오 등 구성 요소를 판별해 행 아이콘을 고르는 정책은 이 경로에 없다.
- Browser 타일은 `ResolveFilePresentation(extension)`이 반환한 **파일 종류별 공통 텍스처**를 그린다.
  개별 모델·재질 썸네일을 그리는 경로로 판정하지 않았다.
  `EditorAssetPresentation::LoadPresentationResources()`가 Model에 `Model.png`,
  Material/Terrain/HDR에 같은 `Texture.png`, Sound에 `Unknown.png` 등을 연결한다.
  확인한 `Model.png`는 배송 상자 그림이고 `Texture.png`는 일반 풍경 기호다.
  작은 에디터 배지 및 실제 에셋 미리보기 역할을 각각 다시 정해야 한다.
- `ContentsBrowserWindow.cpp`의 `kExtensionToIcon`에는 Shader용 `file-contract` 등이 있지만
  이 표는 현재 참조가 없는 선언이다. 이 항목을 화면에 쓰이는 아이콘 결함으로 세지 않는다.

주요 근거: `Editor/EngineGUIWindow/{EditorWindowNames.h,SceneViewWindow.cpp,HierarchyWindow.cpp,
AnimatorEditorWindows.cpp,InspectorWindow.cpp,MenuBarWindow.cpp,ContentsBrowserWindow.cpp}`,
`Editor/EngineEntry/EditorAssetPresentation.cpp`, `Editor/EngineGUIWindow/EditorFontResources.cpp`.
기존 이미지 원본은 `Resources/Editor/Icons`에 있다.

## 선정한 Material Symbols 역할 매핑

FA 열은 이전 대안 비교 기록이다. 적용할 폰트 계열은 Material Symbols이며,
복수 기호를 적은 역할과 3D 변환 표식의 최종 모양은 작은 크기의 제품 화면에서 검증한다.

| 역할 | 교체 전 | FA 대안 기록 | Material Symbols 적용안 |
|---|---|---|---|
| Scene | `users-viewfinder` | `cube` / `layer-group` | `view_in_ar` / `layers` |
| Hierarchy | `bars-staggered` | `sitemap` / `diagram-project` | `account_tree` |
| Inspector | `circle-info` | `sliders` | `tune` |
| AssetBundle | `diagram-project` | `box-archive` | `inventory_2` |
| 선택 | `eye` | `arrow-pointer` | `arrow_selector_tool` |
| 이동 | `arrows-up-down-left-right` | 유지 | `open_with` |
| 회전 | `arrows-rotate` | `rotate-right` | `rotate_right` |
| 스케일 | `group-arrows-rotate` | `up-right-and-down-left-from-center` | `open_in_full` |
| 직교 투영 | `eye-low-vision` | `vector-square` + Ortho 라벨 | `crop_square` + Ortho 라벨 |
| Grid | `bars` | `border-all` | `grid_4x4` |
| 검색 | 일부 `marker` | `magnifying-glass`로 통일 | `search` |
| Controller 상세 | `chess-rook` | `gear` | `settings` |
| Content Browser | 표시 라벨은 이미 `folder` | 유지, 열림 상태는 `folder-open` | `folder` / `folder_open` |

스케일의 대각 화살표와 직교 투영의 사각형은 **일반 기호를 이용한 후보**다.
3D 스케일/투영의 뜻을 기호만으로 완전히 전달한다고 보지 않는다. 툴팁·단축키와 라벨을 유지하고,
정확한 3D 모양이 필요하면 전용 벡터를 사용한다. 회전 아이콘은 방향 변경을 뜻하도록 골라
Refresh/Reimport의 순환 화살표와 구분한다.

`play`, `pause`, `stop`, `folder`, `magnifying-glass`, `camera`, `plus`, `minus`,
`check`, `xmark`, `trash-can`, `lock`, `ellipsis`, `triangle-exclamation`, `terminal` 등은
FA에서도 의미가 잘 맞는다. 폰트 교체를 하더라도 이 역할들의 관습적인 모양은 유지한다.

## 아이콘 폰트 비교 기록

| 후보 | 적합한 부분 | 판단·도입 조건 |
|---|---|---|
| **Material Symbols — 선정** | Scene 도구, 패널, Inspector, 자산/컴포넌트 기호 | Outlined를 시작 스타일로 삼고 상태별 fill·색 정책 고정 |
| **현재 FA 6 Free Solid — 비교 기록** | 재생·검색·폴더·파일·로그·상태·일반 조작 | 비교표의 후보 13개가 현재 TTF cmap에 모두 존재. 주 계열 유지안은 채택하지 않음 |
| **Tabler Icons — 비교 기록** | 가벼운 선형 UI, transform·hierarchy·debug | `rotate-3d`, `arrows-move`, `resize`, `hierarchy` 등을 비교했으며 기본 폰트로 채택하지 않음 |
| **Codicons — 비교 기록** | Console, debugger, source-control, profiler | 개발 도구 역할을 비교했으며 기본 폰트로 채택하지 않음 |

Material Symbols는 Apache 2.0이고 Outlined/Rounded/Sharp 및 가변 축을 제공한다.
현재 제품 통합안은 Outlined의 크기 20, weight 400, fill 0, grade 0을 출발값으로 **고정한 TTF**다.
이 값은 추천 시작점이며 실제 엔진에서 확인한 값은 아니다. 비교표는 공식 24px SVG를 사용했다.
배포 파일과 문자 번호 표는 같은 버전에 고정한다.
[공식 안내](https://developers.google.com/fonts/docs/material_symbols),
[가변 폰트·문자 번호 표](https://github.com/google/material-design-icons/tree/master/variablefont).

Tabler는 공식 webfont 패키지를 제공하며 기본 outline 외에 선 두께 변형과 filled를 구분한다.
폰트 파일은 패키지의 `dist/fonts`에 있다. 라이선스는 MIT다.
[공식 webfont 안내](https://github.com/tabler/tabler-icons/tree/main/packages/icons-webfont).

Codicons는 실제 폰트 배포와 문자 번호 매핑을 제공한다. 아이콘 콘텐츠는 CC BY 4.0,
코드는 MIT로 구분되어 있다. [공식 저장소](https://github.com/microsoft/vscode-codicons).

FA7이나 Pro로 바꾸면 가용 아이콘·스타일 선택지가 달라지지만 역할에 맞는 그림을 고르는 문제는 남는다.
Light/Thin 등은 Pro 전용이고 Free Regular도 모든 Solid 그림의 대체판이라고 가정하면 안 된다.
이번 사례를 해결하기 위해 먼저 Pro를 도입할 필요는 없다.
[공식 스타일 구분](https://docs.fontawesome.com/web/add-icons/how-to).
FA Free의 폰트는 OFL 1.1, SVG/JS 아이콘은 CC BY 4.0이다.
[FA Free 라이선스](https://github.com/FortAwesome/Font-Awesome/blob/6.x/LICENSE.txt).

## Material Symbols 적용 계약

1. `EditorIcon::Select`, `Move`, `Rotate`, `Scale`, `Scene`, `Prefab`, `Search`처럼
   **역할 이름을 정본**으로 둔다. UI 곳곳에서 특정 폰트 이름/문자 번호를 직접 선택하지 않는다.
   역할 → 폰트+글리프 또는 이미지 자원으로 매핑한다.
2. 공통 UI는 Material Symbols로 통일한다. 부족한 엔진 전용 역할은 소수 전용 벡터/이미지로
   보완한다. 변환 도구 세 개나 한 툴바 안에서 서로 다른 무게의 그림을 무작위로 섞지 않는다.
3. 폰트 전체를 같은 글꼴에 무조건 Merge하지 않는다. 서로 같은 문자 번호를 다른 그림에 쓴다.
   실제 확인 예: U+E577은 현재 FA에서는 `square-person-confined`, Material 표에서는 `360`이다.
   별도 아이콘 폰트 참조를 보관하거나, 선택한 문자만 겹침 없이 병합/재배치해야 한다.
   현재 ImGui 1.92.8도 겹침을 위한 `GlyphExcludeRanges`를 제공한다.
4. Material의 웹 예제처럼 문자열 `search`를 넣으면 ImGui에서 자동으로 아이콘이 된다고 가정하지 않는다.
   버전 고정된 codepoint → UTF-8 매핑을 사용한다. 가변 축도 CSS 설정으로 해결하지 않는다.
5. 글자 크기와 그림 폭은 다르므로 공통 아이콘 슬롯·baseline·간격을 정의한다.
   일반 UI는 16–20 logical px, 도구 모음은 20–24px를 출발값으로 실물 검증한다.
   100/150% 사용자 배율 및 OS DPI, hover/active/disabled를 각각 확인한다.
6. 아이콘 선택은 저장 ID와 분리한다. 기존 글리프가 들어 있는 창 이름을 바꿔 layout을 잃지 않도록
   label/안정 ID 및 legacy 매핑을 함께 확인한다. 이미지+텍스트 버튼도 동일한 명령/단축키/툴팁을 유지한다.
7. 폰트 후보가 많아지는 것보다 **동일 역할의 동일 그림**, 같은 유형의 동일 배지,
   실제 등록 글리프가 존재하는지의 검사가 우선이다. `editor.theme` 라벨 검사뿐 아니라
   버튼·트리 행·툴바의 의미 아이콘 목록까지 검사 대상을 넓힌다.

## Content Browser 비동기 썸네일 계약

유형 판정과 썸네일 준비 상태는 서로 독립이다. 현재의 확장자 기반 `ResolveFilePresentation()`만으로는
개별 자산을 식별할 수 없으므로, 유형 표현과 별도로 자산 신원을 받는 썸네일 요청·조회 경로를 둔다.

| 상태 | 타일 표시 | 처리 |
|---|---|---|
| 미요청·대기 | 해당 유형의 Material Symbols 아이콘 | 가시 타일 우선 요청. 같은 키의 작업은 한 번만 등록 |
| 읽기·디코딩·생성·GPU 업로드 중 | 같은 유형 아이콘 | UI는 완료를 기다리지 않고 선택·검색·스크롤을 계속 처리 |
| Ready | 실제 에셋 썸네일 + 작은 유형 배지 | 표시 가능한 결과가 게시된 다음 프레임부터 교체. Ready 캐시는 첫 조회부터 사용 |
| 실패·미지원 | 해당 유형 아이콘 | 매 프레임 재요청하지 않음. 자산 변경 또는 명시적 재시도 시 실패 상태 무효화 |
| 자산 변경·삭제 | 현재 버전에 맞는 유형 아이콘, 삭제한 타일은 제거 | 기존 Ready 결과를 무효화하고 오래된 작업의 늦은 완료를 폐기 |

- **실물 미리보기:** 메시/모델은 해당 형상을 렌더한 이미지, 텍스처는 해당 픽셀의 축소 이미지다.
  Material/Prefab/HDR은 각 유형의 생성기를 같은 요청 계약에 연결한다. 미지원 생성기는 아이콘을 유지한다.
- **작업 경계:** 파일 읽기·CPU 디코딩·축소는 작업 큐에서 수행한다. 모델 렌더·GPU 업로드는 기존
  렌더 자원 소유 경로에 예약한다. 작업 스레드가 ImGui나 렌더 문맥을 직접 호출하지 않는다.
  CPU 이미지가 생겼다는 이유만으로 Ready로 게시하지 않고, 실제 GPU 사용 가능 시점을 따른다.
- **교체 시점:** 완료 큐를 UI 프레임 시작에 반영하고 그 프레임에 같은 타일의 그림만 바꾼다.
  타일 ID·크기·선택·더블클릭·drag-drop·팝업 대상은 유지한다. 로딩용 modal이나 프레임 대기는 두지 않는다.
- **신원·무효화:** 캐시 키는 자산 GUID와 subasset 신원, 원본/의존성 revision, 요청 해상도,
  생성 방식 버전으로 구성한다. 요청 세대가 바뀌면 늦은 결과를 버려 다른 파일이나 구버전 그림이 나타나지 않게 한다.
- **비용·수명:** 가시 타일 우선순위, 중복 억제, 동시 작업 수와 메모리 예산을 둔다.
  캐시는 안정 Texture 신원을 소유하고 `ImTextureID`는 매 프레임 `EditorImGuiTexture`로 해석한다.
  퇴출·종료 시 실행 중 작업과 렌더 참조가 끝나는 기존 자원 해제 규약을 따른다.

완료 판정은 느린 로더에서 아이콘 상태로도 UI 입력이 처리되는지, 준비 후 재탐색 없이 바뀌는지,
동일 자산 요청이 중복되지 않는지, 변경·삭제 뒤 늦은 완료가 재게시되지 않는지를 포함한다.
DX12/Vulkan에서 캐시 퇴출·창 닫기·종료 중 자원 수명도 확인한다. 이 검증은 아직 수행하지 않았다.

## 적용 순서와 단계 경계

1. **W1:** Material Symbols의 버전·라이선스·codepoint·기준 크기를 고정하고 역할 매핑을 만든다.
   창의 표시 아이콘과 저장 ID를 분리한 뒤 패널·툴바·행을 이관한다.
2. **W1:** 선택/스케일/직교/검색/Controller 상세의 의미를 바로잡고,
   Model/Material/Prefab/Script/Audio 등의 유형 아이콘을 같은 체계로 정리한다.
   Live Code 자리표시자는 제거 대상으로 추적하며 대체 아이콘은 만들지 않는다.
3. **W7 Browser:** 위 계약에 따라 비동기 요청·생성·캐시·완료 게시를 구현하고 실제 타일 소비자를 연결한다.
   썸네일 부분은 기존 clipping 작업에 추가되는 범위이며 공수는 별도 산정한다.
4. 폰트 글리프·DPI·저장 레이아웃 회귀와 비동기 썸네일 회귀를 각각 판정한다.
   W1 선정 완료나 기존 테마 회귀 통과만으로 폰트 교체·썸네일 구현까지 완료로 표시하지 않는다.

## 선정 조사 산출물과 검증

- [아이콘 비교 HTML](../../Artifacts/phase21-icon-study/comparison.html)
- [비교 PNG](../../Artifacts/phase21-icon-study/comparison.png) / [SVG](../../Artifacts/phase21-icon-study/comparison.svg)
- `Artifacts/phase21-icon-study/sources/`: 현재 비교에 쓰는 공식 SVG 39개와 라이선스
  (이전 Live Code 비교용 `code.svg` 3개는 조사 캐시에만 남아 있으며 현재 비교·출처 manifest에서 제외)
- `sources.json`: 출처 URL·저장소 commit·SHA-256·현재 폰트 name table
- `coverage.json`: FA 비교 후보 13개가 현재 내장 TTF에 존재함과 Material 적용안의 codepoint 존재 확인

비교표는 Live Code를 제외한 13개 역할 × 현재/FA/Material/Tabler의 4열이며,
Material Symbols 열에 선정 결과를 표시했다. 실제로 렌더한 PNG를 확인했다.
비교표의 CURRENT 열은 교체 전 엔진 블롭에서 추출한 6.4.2 폰트다. FA 후보 SVG는 별도로 고정한 공식 6.x 버전이므로
기존 바이너리의 렌더와 완전히 같은 픽셀이라고 주장하지 않는다. 아이콘 크기와 색을 비교용으로 조정했다.

고정한 저장소 commit:

- Font Awesome: `840c215f894f429b26b8c1402a65da835dc5a450`
- Material: `40a7a292a79d9394157e1ea24f83d52d5e17c556`
- Tabler: `55f87a73f45cf1d9eaf16d7da705065483a9e4f9`

산출물은 조사 자료로 `Artifacts`에 두었으며 제품 자원 배포에는 연결하지 않았다.
선정 조사 시에는 제품 C++/프로젝트 파일을 변경하거나 빌드·런타임 검사를 수행하지 않았다.
그 뒤 제품 적용과 빌드·회귀 결과는 위 W1 검증 기록으로 분리했다.
동시에 진행 중인 다른 UI 변경은 보존했다.
