# Creator Engine 브랜드·API 사이트

브랜드 홈페이지 `index.html`과 `api/` 문서를 같은 로고·색상·내비게이션으로 연결한다.
HTML/CSS/JavaScript와 직접 작성한 Canvas 2D 조명 코드만 사용한다. React Bits·PrSM의
효과 컴포넌트, 레거시 영상, 외부 CDN 스크립트·폰트는 포함하지 않는다. PrSM은 정보 배치만 참고했다.

## 이번 재개 작업에서 보완한 항목

- `Translate` 같은 멤버 이름 검색 결과는 해당 멤버의 고정 링크로 바로 이동한다.
- 존재하지 않는 문서에서 이전 소스 링크를 숨기고, 정상 문서로 돌아오면 복구한다.
- 모바일 문서 메뉴가 열렸을 때 본문을 `inert`로 분리하고 닫기·화면 확장 시 복구한다.
- JavaScript가 없으면 동작하지 않는 모션 버튼은 숨기고 정적 첫 화면을 유지한다.
- 캡처 갤러리는 원본 비율을 유지하면서 로딩 전 공간을 확보하고 실패 안내를 제공한다.
- 코드 시그니처가 본문·초깃값을 생략한 요약이라는 점을 API 본문에 표시한다.

## React 재작성 뒤 고친 항목

- 본문 클래스를 렌더 전에 세운다. effect로 미루면 자식 effect가 먼저 돌아
  `.brand-page main > section`이 빈 결과를 잡고, 절 이동이 기본 스크롤만 막은 채 아무 데도 가지 않았다.
- 휠·키보드는 실제로 이동할 때만 기본 동작을 막는다. 절이 화면보다 길면 그 안쪽과
  마지막 절 아래의 푸터는 브라우저에 맡긴다. 경계 판정에 `scroll-padding-top`을 반영한다.
- 멤버 주소로 연 첫 화면이 문서 맨 위에 떨어지지 않게 `scrollIntoView`를 `instant`로 고정하고
  `requestAnimationFrame`을 거치지 않는다.
- 히어로 배경을 Loading.bmp의 구성에 맞춰 다시 그린다.
- 30 FPS 상한을 실제로 적용하고 숨겨진 탭에서 멈춘다. 이전에는 문서에만 적혀 있었다.
- 빌드가 `docs/` 아래 모든 파일을 배포본에 남긴다. 이전에는 세 경로만 복사해
  대시보드와 분석 문서의 주소가 배포에서 사라졌다.
- 참조되지 않는 `site.js`, `facets.js`, `api/reference-ui.js`를 제거한다.

## 로컬 실행

저장소 루트에서 실행한다.

```powershell
npm install
npm run dev
```

Vite가 표시한 `http://localhost:5173/`을 연다. ES module과 JSON fetch 때문에 `file://`로 열지 않는다.
내부 파일은 상대 경로로 참조하므로 `/CreatorEngine/`에서도 같은 구조를 사용한다.
문서 주소는 `api/#script/Transform`, 멤버 주소는 `api/#script/Transform~member-translate`이다.
해시 라우팅을 사용해 서버의 SPA fallback이나 URL rewrite를 요구하지 않는다.

## 소스 구성

| 파일 | 책임 |
|---|---|
| `index.html`, `api/index.html` | Vite의 랜딩·API 다중 페이지 진입점 |
| `src/main.jsx`, `src/App.jsx` | React 렌더링, 모바일 메뉴, 갤러리/dialog, 문서 검색·해시 탐색 |
| `src/styles.css`, `style.css` | 공통 반응형 디자인 토큰과 레이아웃 |
| `site-assets/mark.svg` | 제공된 Loading.bmp에서 추출한 기존 로고 픽셀 |
| `api/reference.json` | 검토한 문서 31개·주요 멤버 214개와 소스 근거 |
| `resource/*.png` | 사용자 제공 실제 스크린샷. 원본 변경 없음 |

React로 다시 쓰기 전의 `site.js`, `facets.js`, `api/reference-ui.js`는 참조하는 파일이 없어 제거했다.
검색·사이드바·본문·목차·해시 라우팅은 모두 `src/App.jsx`가 담당한다.

기존 엔지니어링 문서와 대시보드는 삭제하거나 이동하지 않는다.
`generate_scriptbinder_docs.py`는 기존 레거시 도구로 남기되 새 사이트에서 실행하지 않는다.

## API 범위와 유지보수

초판은 **선별·수동 검토 레퍼런스**이며 전체 공개 API의 자동 생성 결과가 아니다.
C# 타입 21개, 네이티브 인터페이스 4개, 시작 가이드 5개, CreatorBuildTool 안내를 포함한다.
모든 오버로드·상속 멤버·내부 바인딩의 완전한 열거 또는 안정적인 네이티브 플러그인 ABI를 뜻하지 않는다.

`reference.json`의 `meta.revision`은 소스 기준 커밋이다. 각 페이지의 `source`와
`meta.sourceFiles`에 Git blob 해시를 기록한다. 멤버 행은 `[이름, 서명, 설명, 종류]`이고
문서의 property 서명은 접근 계약을 표현한다. 실제 함수 본문을 선언처럼 복사한 것은 아니다.

검사 도구는 참조한 파일의 내용이 바뀌면 실패한다. 문서를 검토한 뒤 본문·기준 커밋·관련 해시를
함께 갱신해야 하며, CI를 통과시키려고 해시만 자동 변경하지 않는다. 구조·기능 설명이 바뀌어도
같이 검토한다. 소스에 남아 있는 과거 주석을 현재 계약으로 가정하지 않는다.
예를 들어 Transform은 class이고, BlackBoard.Set*는 항목을 덮어쓸 수 있다.

## 검사와 빌드

```powershell
npm run build
```

Vite는 `docs/dist`에 랜딩과 `api/index.html`을 생성하고, `docs/` 아래의 나머지 파일을
같은 상대 경로로 함께 옮긴다. `RefactoringPlanDashboard.html`, `README.md`, `analysis/`,
`design/`, `plans/`처럼 이전부터 게시되던 주소가 배포에서 빠지지 않게 하기 위한 것이다.
빌드 입력인 `src/`만 배포본에서 제외한다. 원본 스크린샷 3개는 별도 외부 다운로드 없이
checkout의 파일을 사용한다.

자동화된 사이트 검사 도구는 이 저장소에 없다. 이전 판에는 `Tools/site/check.py`와
Playwright 회귀 스크립트를 실행하라고 적혀 있었지만 해당 파일은 존재한 적이 없다.
현재 확인은 `npm run build` 뒤 `docs/dist`를 로컬 HTTP로 띄워 직접 보는 것으로 한다.

```powershell
npm run build
python -m http.server 8791 --directory docs/dist --bind 127.0.0.1
```

마지막으로 손으로 확인한 항목은 다음과 같다. 네이티브 Windows 빌드, C# 예제 컴파일·실행,
실기기 성능 벤치마크, 실제 Pages 게시는 여기에 포함되지 않는다.

- 랜딩에서 휠·PageDown 한 번에 절 하나씩 이동하고 마지막에 푸터까지 닿는다.
- 절이 화면보다 길면 그 안쪽 스크롤은 브라우저에 맡긴다.
- `api/#script/Transform~member-translate`처럼 멤버 주소로 바로 열면 해당 멤버에 도착한다.
- 1440px과 375px에서 가로 넘침이 없고 두 페이지 모두 콘솔 오류가 없다.

## 배포 경계

`.github/workflows/website.yml`은 Node를 설정하고 `npm ci`, `npm run build`를 실행한 뒤
`docs/dist`를 GitHub Pages artifact로 업로드하고 `actions/deploy-pages@v4`로 게시한다. `docs/**`가 변경되거나 수동 실행할 때 동작하며,
기존 엔진 빌드 workflow와 분리되어 있다.

**저장소 설정을 먼저 바꿔야 한다.** 현재 Pages 배포 원본은 아직 `build_type: legacy`
(브랜치 `master`, 경로 `/docs`)다. 이 상태에서는 legacy 배포기와 위 workflow가 같은 커밋에
동시에 돌아 늦게 끝난 쪽이 이긴다. legacy가 이기면 빌드를 거치지 않은 `docs/index.html`이
그대로 서빙되고 그 안의 모듈 주소가 404가 되어 빈 화면이 된다. 이 사이트를 커밋하기 전에
저장소 설정의 Pages 배포 원본을 **GitHub Actions**로 지정한다.

게시가 완료되면 workflow의 `github-pages` environment에 실제 Pages URL이 표시된다.

## 이미지·모션·접근성

Loading.bmp는 새로 그리지 않고 기존 로고만 분리했다. 각진 배경은 그 이미지의 구성을 따르되
픽셀을 베끼지 않고 좌표로 다시 구성한다. 원본에서 읽은 기준은 강조 색상 hue 약 210°,
최고 하이라이트 `#A8F7FF`, 150을 넘는 밝은 화소가 전체의 4% 남짓, 아래쪽 1/3은 순수 검정이다.
그래서 면은 화면 가장자리에서 안쪽으로만 뻗고, 중앙과 하단은 본문 가독성을 위해 검정으로 비우며,
테두리는 빛을 정면으로 받은 면에만 남기고 얇은 청백색 균열 다섯 줄을 따로 그린다.
물결·영상이나 엔진의 실제 GPU 렌더링 결과가 아니다. 최대 30 FPS, DPR 1.5로 제한하고
수동 일시정지·모션 감소 설정·숨겨진 탭·화면 밖에서는 애니메이션을 멈춘다.
API 페이지는 Canvas를 생성하지 않는다.

이미지는 `0fb9f7f`의 원본 3개를 참조한다. 처음부터 전부 선로딩하지 않지만 선택한 PNG의
원본 전송량은 남는다. 압축본을 추가할 때도 원본은 보존한다. 확인하지 않은 화면 기능을
캡션으로 꾸며 넣지 않는다. 키보드 탐색·Escape·포커스 복원·skip link·alt를 제공한다.

이 웹사이트 변경으로 엔진 전체나 사용자 이미지에 새 라이선스를 부여하지 않는다.
외부 엔진 의존성·SDK·자산의 기존 조건은 각각 확인해야 한다.

## 원격 반영 상태와 적용 방식

이 사이트는 아직 원격에 올라가 있지 않다. 원격에는 `docs/`로 시작하는 브랜치가 없고,
`master`의 `docs/`는 이전 판이다. 빌드 입력인 `package.json`, `package-lock.json`,
`vite.config.js`, `docs/src/`는 아직 추적되지 않으므로, 수정된 파일만 커밋하면 CI의
`npm ci`가 실패한다. 커밋할 때 이 넷을 함께 넣는다.

변경은 웹사이트 파일과 읽기 권한의 배포 workflow에 한정된다. 엔진 C++/C# 소스,
사용자 스크린샷 원본, 기존 계획서와 대시보드는 포함하거나 덮어쓰지 않는다.
문서 소스 기준은 `b503b57`이며, `4d6122e`까지의 비교에서 문서화한 API 소스 변경은 없었다.
