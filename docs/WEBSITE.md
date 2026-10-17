# 문서 웹사이트의 구성과 배포

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`, 2026-10-15. 아래 내용은 저장소 소스의 연결을 확인한 설명이다. 이번 문서 재작성에서 npm 설치·Vite 빌드·브라우저 실행·Pages 배포는 하지 않았다.

## 두 진입점, 하나의 React 애플리케이션

[docs/index.html](index.html)과 [docs/api/index.html](api/index.html)은 각각 `src/main.jsx`를 불러온다. [App.jsx](src/App.jsx)는 URL pathname에 `/api/`가 포함되는지로 소개 화면과 API 화면을 고른다. 별도의 서버 라우터는 없다.

- 소개 화면은 역사 Editor 캡처 세 장, 엔진 구성 안내와 API 링크를 표시한다
- API 화면은 현재 디렉터리의 `reference.json`을 HTTP fetch한다. 파일을 읽지 못하면 오류 화면이 나오며, 개별 페이지를 로드하는 서버 API로 우회하지 않는다
- 이전 section/member hash가 새 선별 문서에서 사라졌으면 `legacyAnchors`가 현재 계약 절로 연결하고 이전 링크 안내를 표시한다. 제거된 시그니처를 지원 API처럼 복원하지 않는다
- 페이지 신원은 `#guide/overview`, `#script/Component` 같은 hash다. `~members` 또는 `~member-...` 부분이 문서 내부 anchor를 가리킨다
- 검색은 로드한 페이지의 제목·요약·멤버·절 내용을 브라우저에서 필터한다. 저장소 전체 코드/문서 검색 기능은 아니다
- 알 수 없는 페이지 신원은 찾을 수 없음 안내를 표시한다. 빈 hash는 `guide/overview`다

본문 데이터는 [api/reference.json](api/reference.json)의 `meta`와 `pages`다. 각 페이지의 `source`와 `meta.revision`으로 GitHub 불변 소스 URL을 만든다. 따라서 revision만 새 값으로 바꾸고 오래된 설명을 남겨서는 안 된다. 선별 API 설명이며 전체 자동 생성 SDK 문서를 주장하지 않는다.

## 정적 엔지니어링 문서

[문서 지도](RefactoringPlanDashboard.html)와 `analysis/`, `design/`, `plans/`는 별도 파일이다. Vite가 Markdown을 HTML 문서로 변환하는 구성은 없다. Markdown의 풍부한 렌더링이 필요하면 저장소의 해당 파일을 GitHub에서 읽는다. HTML 문서는 그 파일의 자체 표시를 사용한다.

[vite.config.js](../vite.config.js)의 `copy-doc-assets`는 빌드 후 `docs` 최상위 항목을 `docs/dist`로 복사한다. `dist`, `src`, `node_modules`, 루트 `index.html`은 제외하고 `api/index.html`도 원본 복사에서 제외한다. 두 HTML 진입점은 Rollup 입력으로 따로 빌드된다. `base: './'`이므로 배포물 asset 링크는 상대 경로를 사용한다.

원래 문서 파일 경로를 제거하면 기존 직접 URL이 깨진다. 내용을 통합할 때도 호환 안내 파일을 두고, HTML `id`와 API hash 링크는 별도로 검사한다. 파일 경로가 존재하는 것만으로 anchor까지 유효하다고 판단하지 않는다.

## 개발 명령

저장소 루트의 [package.json](../package.json)은 React 19와 Vite 7 계열을 선언하며 정확한 설치 해석은 [package-lock.json](../package-lock.json)에 있다. 현재 workflow가 지정하는 Node는 22다.

```sh
npm ci
npm run dev
npm run build
npm run preview
```

`dev`는 `vite --host 0.0.0.0`이므로 모든 인터페이스에 bind한다. 신뢰하지 않는 네트워크에 그대로 노출하지 않는다. `build`는 `docs/dist`를 생성하며, 엔진 C++/C#을 빌드하지 않는다. `preview`는 생성된 정적 웹 결과 확인용이고 Editor/Player 실행 검증이 아니다.

## 배포 조건

[website.yml](../.github/workflows/website.yml)은 다음 조건에서 작동한다.

1. `master` push에서 `docs/**`, package 파일, Vite 설정 또는 workflow 자신이 바뀜
2. 또는 수동 `workflow_dispatch`
3. Ubuntu runner에서 checkout → Node 22 → `npm ci` → `npm run build`
4. `docs/dist`를 Pages artifact로 업로드하고 GitHub Pages에 배포

이 workflow에는 `pull_request` 트리거가 없다. PR에 자동 검사가 없다는 사실을 빌드 통과로 기록하면 안 된다. 배포에는 Pages/ID token 권한과 `github-pages` environment가 사용된다. PR 작성이나 docs commit만으로 실제 배포가 완료되었다고 보지 않는다.

## 역사 리소스와 접근성

`resource/` 캡처는 2026-09-14 자료이며 파일 자체와 표시 날짜를 유지한다. `site-assets/mark.svg`와 CSS는 표시 리소스다. 소개 애니메이션은 reduced-motion, visibility와 intersection 상태를 확인하며, 수동 일시정지 버튼이 있다. 이미지 확대는 dialog를 사용할 수 있으면 열고, 그렇지 않으면 원본 이미지 링크로 이동한다. 이런 코드 경로의 존재와 실제 브라우저·키보드·모바일 사용성 검증은 별개다.

## 폐기된 생성 경로

[generate_scriptbinder_docs.py](generate_scriptbinder_docs.py)는 현재 웹 레퍼런스를 생산하지 않는다. 입력은 존재하지 않는 루트 `ScriptBinder`, 출력은 루트 `API_DOCS`다. 현재 문서 데이터는 명시적으로 소스를 확인하여 작성한다. 새로운 생성기로 대체하려면 별도 도구 작업과 테스트가 필요하다.
