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

## 로컬 실행

저장소 루트에서 실행한다.

```powershell
python -m http.server 8080 --directory docs
```

`http://localhost:8080/`을 연다. ES module과 JSON fetch 때문에 `file://`로 열지 않는다.
내부 파일은 상대 경로로 참조하므로 `/CreatorEngine/`에서도 같은 구조를 사용한다.
문서 주소는 `api/#script/Transform`, 멤버 주소는 `api/#script/Transform~member-translate`이다.
해시 라우팅을 사용해 서버의 SPA fallback이나 URL rewrite를 요구하지 않는다.

## 소스 구성

| 파일 | 책임 |
|---|---|
| `index.html`, `style.css` | 브랜드 소개와 공통 반응형 디자인 |
| `site.js` | 모바일 메뉴, 스크린샷 전환, 원본 확대 dialog |
| `facets.js` | 고정 기하학적 면의 이동 광원·포인터 반응 |
| `site-assets/mark.svg` | 제공된 Loading.bmp에서 추출한 기존 로고 픽셀 |
| `api/index.html`, `api/reference-ui.js` | 검색·사이드바·본문·목차·해시 라우팅 |
| `api/reference.json` | 검토한 문서 31개·주요 멤버 214개와 소스 근거 |
| `resource/*.png` | 사용자 제공 실제 스크린샷. 원본 변경 없음 |

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
python Tools/site/check.py
python Tools/site/build.py
```

`Build/Website`에 새 출력을 만든다. 기존 출력은 자동 삭제하지 않는다. 다른 경로가 필요하면
`--output`을 사용한다. 빌드는 기존 docs 트리도 보존한다. 원본 스크린샷 3개는 별도 외부 다운로드
없이 checkout의 파일을 사용한다.

소스 저장소 없이 문서 overlay만 검토할 때는 `--preview`가 누락된 엔진 소스와 이미지 바이트를
명시적으로 보고한다. 실제 저장소 검증에는 이 옵션을 사용하지 않는다.

선택적 브라우저 회귀와 미리보기:

```powershell
python -m pip install -r Tools/site/requirements-browser.txt
python -m playwright install chromium
python Tools/site/browser_test.py
python Tools/site/refinement_test.py
python Tools/site/offline_preview.py
```

결과는 `Build/WebsiteTests/`에 기록한다. 이 테스트는 실제 HTML/CSS/JS를 메모리 DOM에 로드하고
reference.json을 fixture로 공급한다. **HTTP 호스팅·실제 이미지 디코딩·Pages 게시 검사를
대체하지 않는다.** 360/390/768/1440/1920 폭의 문서 라우트·가로 넘침, 검색, 뒤로/앞으로 이동,
멤버 링크, 코드 복사 fallback, 메뉴·dialog·포커스, 조명 일시정지·모션 감소·화면 밖 정지를 검사한다.
재개 작업에서 기존 237개 단정과 보완 검사 31개 단정이 통과했다. 네이티브 Windows 빌드, C# 예제 컴파일·실행,
실기기 성능 벤치마크는 수행하지 않았다.

별도 실제 HTTP 브라우저 검사도 준비되어 있다.

```powershell
python Tools/site/http_test.py
```

이 검사는 실제 원본 스크린샷이 있는 전체 저장소에서 실행해야 한다. 문서 파일만 있는
환경에서는 `--allow-missing-assets`가 세 이미지의 검증 제외를 명시한다. 이번 작성 환경에서는
HTML/CSS/JS/JSON 여섯 파일의 로컬 HTTP 상태·MIME 확인까지만 완료했고, Chromium의 HTTP
내비게이션은 `ERR_BLOCKED_BY_ADMINISTRATOR`로 차단됐다. 이 검사를 통과했다고 기록하지 않는다.
브라우저 정책을 바꾸거나 다른 주소로 우회하지 않았다. 원본 이미지 다운로드·디코딩,
네이티브 빌드 및 실제 Pages 게시도 이번 검증에 포함되지 않는다.

## 배포 경계

`.github/workflows/website.yml`은 `master`의 `docs/`를 GitHub Pages artifact로 업로드하고
`actions/deploy-pages@v4`로 게시한다. `docs/**`가 변경되거나 수동 실행할 때 동작하며,
기존 엔진 빌드 workflow와 분리되어 있다. GitHub 저장소 설정의 Pages 배포 원본은
**GitHub Actions**로 지정해야 한다.

별도 웹 프레임워크 빌드나 Node 패키지 설치는 필요 없다. 게시가 완료되면 workflow의
`github-pages` environment에 실제 Pages URL이 표시된다.

## 이미지·모션·접근성

Loading.bmp는 새로 그리지 않고 기존 로고만 분리했다. 각진 배경은 별도 정규화 좌표로 구성하며
면별 법선과 이동 광원으로 명암을 계산한다. 물결·영상이나 엔진의 실제 GPU 렌더링 결과가 아니다.
최대 30 FPS, DPR 1.5로 제한하고 수동 일시정지·모션 감소 설정·숨겨진 탭·화면 밖에서는
애니메이션을 멈춘다. API 페이지는 Canvas를 생성하지 않는다.

이미지는 `0fb9f7f`의 원본 3개를 참조한다. 처음부터 전부 선로딩하지 않지만 선택한 PNG의
원본 전송량은 남는다. 압축본을 추가할 때도 원본은 보존한다. 확인하지 않은 화면 기능을
캡션으로 꾸며 넣지 않는다. 키보드 탐색·Escape·포커스 복원·skip link·alt를 제공한다.

이 웹사이트 변경으로 엔진 전체나 사용자 이미지에 새 라이선스를 부여하지 않는다.
외부 엔진 의존성·SDK·자산의 기존 조건은 각각 확인해야 한다.

## 원격 반영 상태와 적용 방식

원격 브랜치 `docs/brand-api-site-20260916`은 재확인 시 `b503b57`에 머물러 있다.
브랜치 갱신 요청이 연결의 안전 검사에서 차단되어 새 사이트 커밋·PR은 생성되지 않았다.
이 파일 묶음은 기존 저장소에 검토하여 적용하는 로컬 변경본이며, 내려받거나 실행하는 것만으로
GitHub 설정·브랜치·배포를 변경하지 않는다. 토큰 또는 추가 자격 증명을 요구하지 않는다.

변경은 웹사이트 파일과 `Tools/site` 검사 도구, 읽기 권한의 검증 workflow에 한정된다.
엔진 C++/C# 소스, 사용자 스크린샷 원본, 기존 계획서와 대시보드는 포함하거나 덮어쓰지 않는다.
문서 소스 기준은 `b503b57`이며, `4d6122e`까지의 비교에서 문서화한 API 소스 변경은 없었다.
