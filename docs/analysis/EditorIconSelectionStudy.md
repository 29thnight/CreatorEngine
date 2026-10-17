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
DX12/Vulkan에서 캐시 퇴출·창 닫기·종료 중 자원 수명도 확인한다.

### 구현이 이 계약을 고친 자리 (2026-09-16, PHASE 21 W7-6)

위 문단은 착수 전에 쓴 것이고, 구현하면서 세 곳이 실제와 달라졌다. 계약을 고치지 않고
두면 다음 사람이 여기 적힌 것을 읽고 없는 것을 찾는다.

- **캐시 키는 자산 GUID 가 아니다.** 경로 해시 + `revision`(`last_write_time ^ file_size`)
  을 쓴다. GUID 를 얻으려면 타일마다 `.meta` 를 읽어야 하는데, 그것은 W7-5 가 방금 닫은
  결함(스캔 밖에서 프레임마다 디스크를 만지는 것)을 그대로 되살린다. `revision` 은 목록
  스캔이 `directory_iterator` 결과에서 이미 들고 있는 값이라 새 접촉이 0 이다. 대가는
  하나뿐이다 — 파일 이름을 바꾸면 같은 그림을 한 번 더 디코딩한다.
- **`subasset` 은 키에 자리만 있고 항상 0 이다.** 지금 생성기는 이미지 디코딩 하나뿐이라
  한 파일이 그림 하나를 낸다. 텍스처 아틀라스·머티리얼 슬롯이 생길 때 채운다.
- **무효화는 UI 사건이 아니라 목록에서 유도한다.** 같은 경로인데 `revision` 이 다른 항목이
  있으면 그 파일은 바뀐 것이고, 목록 스캔이 이미 그렇게 말하고 있다. 삭제 메뉴·감시자 같은
  사건 쪽에 걸면 놓치는 경로가 생기지만 이쪽은 **새 키가 생기는 모든 경우**를 덮는다.

그리고 계약에 없던 자가 둘 필요했다.

- `IsTextureReady` — *"실제 GPU 사용 가능 시점을 따른다"* 를 물을 수단이 없었다.
  `RegisterTexture` 는 업로드 전에도 0 이 아닌 id 를 돌려준다(DX12 는 프레임이 닫혀 있으면
  서술자 칸만 예약하고 널 SRV 를 쓴다). 백엔드 두 팔에 세웠다.
- `thumbnail_set_budget_bytes` / `editor.thumbnail budget` — *"메모리 예산을 둔다"* 를
  **자극할 방법이 없었다.** 목록은 clipper 로 보이는 타일만 요청하므로 파일을 수백 개
  뿌려도 기본 48 MB 에 닿지 않고, 예산은 `constexpr` 이라 밖에서 낮출 수도 없었다.

검증은 `Tools/regression/verify-browser-thumbnail-contract.ps1` 이 한다(대조 56 단정 ·
변이 5 종 전부 포획 · run-all 배선). **다만 전부 검증되지는 않았다.**

- *"느린 로더에서 UI 입력이 처리되는지"* 는 느린 로더를 만들 창구가 없어 직접 자극하지
  못했다. 대신 디코딩이 워커 풀 안에서만 불린다는 것을 소스 축으로 지킨다.
- *"늦은 완료가 재게시되지 않는지"*(`lateDropped`)는 진행 중인 작업이 있는 바로 그 순간에
  무효화가 닿아야 서는 축이라 프레임 단위로 확률적이다. 수는 내되 판정하지 않는다.
- Vulkan 은 이 페이즈의 대상이 아니다. 런타임 회차는 DX12 뿐이고, Vulkan 팔은 **배선이
  있는가**만 소스로 지킨다.

### 모델 렌더 썸네일 정찰 (2026-09-16)

W7-6 이 남긴 하나뿐인 항목이다. 착수 전에 정찰만 했다 — **코드는 쓰지 않았다.**
아래는 전부 실물을 읽어 확인한 것이고, 확인하지 못한 것은 그렇게 적었다.

#### 결론부터

착수 가능하다. **4~5일.** 착수 전에 알아야 할 것은 셋이다.

1. draw 밀봉도 재질 변환도 **씬을 요구하지 않는다** — 둘 다 이미 있는 함수다.
2. 썸네일 패스는 **라이브 프레임 그래프 안**에 들어가야 한다(프레임 밖이 아니다).
3. 결과는 **구워서 `Library/` 에 둔다.** 그래야 평상시 비용이 0 이고 게이트가 결정적이다.

#### ① 씬 없이 그릴 수 있는가 — 된다

`BuildRHIModelMeshView(generation, meshIndex, view)` 는 `RHI/IRenderDeviceServices.h`
의 **inline 자유 함수**다. 생성물과 메시 인덱스만 받는다.

제품이 밀봉하는 자리(`EnhancedSceneRenderer.cpp` 의 `poolMesh`)가 프록시에서 읽는
것은 **`m_worldMatrix` · `m_modelGeneration` · `m_modelMeshIndex` 셋뿐**이다. 앞의
둘은 썸네일이 만들고 뒤는 루프 변수다. `EnhancedDrawItem` 자체가 *"프레임을 밀봉할
때 필요한 것만 복사해 온"* POD 라 씬 자료구조를 안 들고 있다.

#### ② 재질 텍스처는 얼마나 드는가 — 네 줄이다

처음에는 무광(베이스컬러 팩터 + 방향광 하나)으로 시작하고 재질은 나중에 올리자고
적었다. **그 판단이 틀렸다.** `baseColorFactor` 는 대부분의 자산에서 `1,1,1,1` 이고
신원은 전부 `baseColor` **텍스처**에 있다. 무광으로 가면 "덜 예쁜" 것이 아니라
**모델 50 개가 거의 같은 회색 덩어리로 보인다** — 썸네일의 일이 식별인데 그것을
못 한다.

그리고 비용도 과대 계상이었다. `ModelSceneInstantiation.cpp:160-167` 이 모델
생성물에서 렌더 가능한 재질을 만드는 전 과정을 이미 하고 있다.

```cpp
ExperimentMaterialMigration::ConvertModelMaterialAsset(materials[index], *generation, *converted);
ExperimentMaterialMigration::ConvertToLegacyMaterial(*converted, nullptr, *material, error);
DataSystems->FinalizeMaterialRuntime(*material);
DataSystems->BindModelGenerationTextures(*material, *generation);
```

넷 다 공개 창구고 씬을 요구하지 않는다. 나오는 `shared_ptr<Material>` 이 제품
밀봉이 먹는 `pooled.materialSource` 이고, 남는 `converted` 가
`pooled.authoredMaterialSource` 다. 내장 텍스처는
`DataSystem::ResolveModelGenerationTexture` 가 자기 캐시를 갖고 푼다.

즉 재질은 **새로 짓는 것이 아니라 부르는 것**이다.

#### ③ 언리얼은 어떻게 하는가, 그리고 왜 그대로 베끼면 안 되는가

| 층 | 언리얼 |
|---|---|
| 렌더러 선택 | `UThumbnailManager` 가 자산 클래스 → `UThumbnailRenderer` 표를 든다 |
| 그리는 법 | `FThumbnailPreviewScene` 파생이 **진짜 `FPreviewScene`(최소 `UWorld`)** 을 세우고 제품 씬 렌더러를 돌린다. 카메라는 바운드에서 유도하되 `USceneThumbnailInfo`(orbit pitch/yaw/zoom)가 **자산에 저장**되어 사용자가 돌려 놓은 각도가 유지된다 |
| 표시 | `FAssetThumbnailPool` 이 렌더 타깃을 재활용하고 Slate 가 그 RT 를 **직접 샘플링**한다. 프레임당 **시간 예산**으로 몇 장을 그릴지 정한다 |
| 저장 | `.uasset` 패키지의 thumbnail table(`FObjectThumbnail`, `ThumbnailTools::CacheThumbnail`). 그래서 **평상시 브라우징은 아무것도 렌더하지 않는다** |

★ **미니 월드는 우리 구조에서 더 비싸다.** 언리얼 렌더러는 `UWorld` +
`FSceneViewFamily` 를 먹으므로 월드를 세우는 것이 가장 싼 길이다. 우리는 게임
스레드가 프레임을 **한 번** 밀봉하고 모든 뷰가 그 draw 목록을 공유한다
(`EnhancedLiveViewPacket` 에는 draw 목록이 없다 — key·camera·gizmos·target·flags
뿐이다). "미니 월드" 는 곧 두 번째 Scene 을 만들고 두 번째 프레임을 밀봉하는
것인데 그런 장치가 없다. 반대로 ①②가 보인 대로 **합성이 언리얼보다 싸다.**

☞ 가져올 것: 시간 예산(개수 예산이 아니다), 자산별 프레이밍 저장(나중), 구워 두기.
☞ 안 가져올 것: 미니 월드, GPU 잔류(아래).

※ 위 표의 구조는 확실하나 **상수(풀 크기·프레임 허용치 밀리초)는 기억에서 꺼낸
것이고 버전을 탄다.** 설계 근거로 쓸 값이면 소스를 열어 확인할 것.

#### ④ GPU 잔류 — 가능하지만 하면 안 된다

기계장치는 있다. `CreateDisplayTexture(w, h, rhiTexture, interopToken)` 가 공유
텍스처를 만들고, 펜스 완료로 슬롯을 표시로 승격하며(`DisplaySlot.fenceValue`),
셸이 `OpenSharedTexture(HANDLE)` 로 받아 `ImTextureID` 를 낸다. 라이브 씬 뷰가 매
프레임 그 길로 온다.

그런데 넷이 동시에 걸린다.

- `ImGuiDx12Shell::RegisterTexture` 는 `textureCache.GetOrUpload(texture)` 로
  **CPU 픽셀을 셸 디바이스에 올린다.** `Texture*` 로는 잔류가 불가능하고, 잔류하려면
  `ImTextureID` + 공유 핸들이라는 **둘째 썸네일 형태**를 캐시에 들여야 한다.
- **Vulkan 에서는 아예 안 된다.** 셸 주석이 명시한다 — *"Vulkan 같은 비-DX12 RHI의
  최종 화면은 CPU 리드백 뒤 이 표를 거쳐 셸 디바이스의 RGBA8 텍스처가 된다."*
  라이브 뷰조차 거기서는 리드백이다. 팔이 둘로 갈린다.
- 썸네일마다 공유 핸들 + 디스크립터. 언리얼이 풀을 묶어 두는 이유가 이것이다.
- 매 실행 다시 그린다.

★ **그리고 굽기가 잔류를 지운다.** 구우려면 CPU 픽셀이 필요하고, 그 순간 잔류의
유일한 이점(왕복 한 번 절약)이 사라진다. 둘은 보완재가 아니라 **대체재**다.

#### ⑤ 구워서 둔다

| 확인한 것 | 값 |
|---|---|
| 파생물 자리 | `Dynamic_CPP/Library/` — `.gitignore` 557행. 이미 `ModelAssetGenerations/<GUID>` 가 산다 |
| `.meta` | **추적 대상**이다(`.gitignore` 140·464행 + 예외 목록). 여기 바이너리를 박으면 git 이 요동친다 — 언리얼이 `.uasset` 에 넣는 것은 그쪽 자산이 원래 바이너리라서다 |
| PNG 쓰기 | `stbi_write_png` 가 이미 에디터에 링크돼 있다(`EditorAssetDatabase.cpp:841`) |

그래서 `Library/Thumbnails/<경로해시>-<revision>.png`. 이름에 `revision` 을 넣으면
**무효화가 공짜**고(바뀐 파일은 다른 이름이 된다) GUID 를 읽지 않아 W7-5 계약을 안
건드린다. 대가는 고아 파일이 쌓이는 것 — 주기적 청소 한 번.

굽고 나면 **런타임 경로 추가분이 0 이다.** 구운 산출물은 그냥 PNG 라 지금 도는
`generator::texture` 가 그대로 판다. 그리고 게이트가 **결정적**이 되어, W7-6 이
적어 둔 *"픽셀 골든을 세우지 마라"* 제약이 통째로 사라진다.

#### ⑥ 패스는 프레임 안에 — 그래서 가장 큰 위험이 사라졌다

재질 밀봉 블록이 `EnsureShaderMetaVariant(context, …)` · `sceneEpoch` · `frameId` ·
`CommitShaderMetaFrame` 에 밀착돼 있다. 프레임 문맥 없이는 못 돈다. 그래서 썸네일
패스는 **라이브 프레임 그래프 안**에 들어간다 — 일이 있을 때만 작은 타깃 하나를 더
그리고 리드백으로 내린다.

이것이 오히려 낫다. 정찰 내내 유일한 큰 위험이 *"프레임 밖 패스를 라이브 펜싱을 안
건드리고 돌릴 수 있는가"* 였는데, 안으로 들어가면 **그 물음 자체가 없어진다.**
라이브 프레임의 펜스·승격 기계를 그대로 탄다. 덤으로 프레임당 예산에 자연히 묶이는데
그것이 언리얼 `FAssetThumbnailPool` 의 시간 허용치와 같은 자리다.

★ 참고로 `Editor/RenderTests` 의 테스트 스물몇 개가 오프스크린+리드백으로 돌지만
**자기 `DX12DeviceResources` 를 새로 만든다.** 에디터 런타임에는 못 쓰고 패스의
모양을 베끼는 용도로만 쓴다.

#### 산정과 남은 미지수

| 조각 | 일 |
|---|---|
| 생성물 로드 + 재질 네 줄 + AABB 프레이밍 + draw 밀봉 | 1 |
| 라이브 그래프 안 썸네일 패스 + 리드백 | 1.5~2 |
| 굽기·조회 계층 | 0.5~1 |
| 캐시 배선(`generator::model`, 구운 뒤 기존 경로 합류) | 0.25 |
| 게이트 | 1 |

★ **남은 미지수 하나.** 프레임의 `EnhancedShaderMetaFrameSnapshot` 은 **그 프레임의
씬이 쓰는 셰이더**로 게임 스레드가 밀봉한다. 썸네일 재질의 셰이더가 그 집합에 없으면
밀봉이 실패하는데, 씬에 없는 모델을 그리는 일이므로 실제로 자주 없을 것이다. 그
셰이더를 프레임 스냅샷에 끼워 넣는 일의 크기가 1.5 일과 2 일을 가른다. 구체적이고
찾을 수 있는 문제라 착수 첫날에 판가름 난다.

부차적으로, `subasset` 축은 여전히 0 이다. 모델 하나가 메시 여럿을 갖지만 썸네일은
모델 전체를 한 장으로 잡으므로 서브에셋이 생기지 않는다.

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
