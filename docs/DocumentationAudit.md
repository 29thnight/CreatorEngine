# 문서 재작성 검토 기록

## 기준과 범위

이 기록은 `493536e300e274cbf59989cdd62b20310c8ae64f`의 구현을 기준으로 2026-10-15에 수행한 문서 검토를 설명한다. `docs/`의 원래 403개 파일을 분류하고, Markdown 287개와 HTML 15개의 본문을 현재 소스 계약에서 새로 작성했다. 웹 선별 API 31페이지와 랜딩 설명도 같은 기준으로 교체했다. 이 숫자는 엔진 기능의 완성도나 실행 수용률이 아니다.

원래 파일 중 304개를 변경했고 99개는 원본 bytes를 유지했다. 변경한 304개는 본문 302개와 웹 API JSON·랜딩 소스 각 1개다. 99개 보존 파일에는 역사 자료 93개와 웹 기반 파일·퇴역 도구 6개가 포함된다. 감사 보고·coverage·검사기 3개를 추가해 최종 docs 파일은 406개다.

파일별 판정은 [DocumentationCoverage.json](DocumentationCoverage.json)에 있다. 각 원래 경로의 이전/현재 Git blob, 교체 또는 보존 사유, 확인한 동작, 근거 소스와 한계를 기록한다. API는 한 JSON 안의 31페이지이며 Markdown/HTML 302개 수에 더하지 않는다.

## 검토 방법

- 선언만 읽지 않고 Editor/Player bootstrap, RuntimeFrame, 실제 renderer 조립, managed/native 호출, asset authoring·cook·publish·load, CLI dispatch의 연결을 따라 확인했다. 파일별 `key_verified_claims`, `source_paths`, 해당하는 `entry_points`가 이 검토의 요약이다.
- 제품 호출 경로와 회귀 helper, 선택적 backend/SDK, 지원하지 않는 기능을 구분했다. 소스에 상수나 경로가 있다는 사실을 해당 환경의 실행 성공으로 바꾸지 않았다.
- 설계·계획 문서도 현재 구현 상태로 다시 썼다. 여전히 유효하지만 연결되지 않은 요구는 미래 의도로 남겼다. 역사 보고서는 당시 결과의 범위와 현행 계약과의 차이를 설명하며, 과거 수치로 최신 성능을 주장하지 않는다.
- 해시와 파일 존재 검사는 인용 대상과 파일 보존의 무결성을 확인한다. 이것만으로 설명의 의미가 맞음을 증명하지 않는다. 문서별 의미 검토와 정적 검사를 서로 다른 근거로 취급한다.

## 주요 계약 정정

- Editor와 Player는 공통 bootstrap·simulation을 사용하지만 저작 UI, backend 선택, presentation과 종료 책임이 다르다.
- SceneHost 제품 경로는 METEOR와 cooked meshlet/mesh-indirect 계약이다. 예전 indexed fallback·구형 pass 목록을 현재 정상 경로로 안내하지 않는다.
- Material Graph의 Scene 실행 지원 부분집합, generation publication, capability/error 경계와 Code authoring·회귀 helper를 구분한다.
- Texture는 descriptor 저장, CPU pixel lifetime, upload submission, GPU 준비를 구분한다. 자동 저장과 충돌 처리를 명시한다.
- 관리 PrePhysics/PostPhysics는 네이티브 고정 물리 스텝 수와 같은 뜻이 아니다. 현재 Component/Entity/Scope 수명과 미지원 과거 API를 명시한다.
- 배포 candidate와 검증·게시를 구분한다. 미구현 Launcher/project descriptor와 실제 CLI를 혼동하지 않는다.
- 일반 frame debugger, 완성 lightmap baker, PT/Hybrid 및 관련 미래 요구를 구현 완료로 표현하지 않는다.

## 보존과 링크 호환

역사 측정 JSON, 이미지·애니메이션·도식과 command disposition TSV는 재측정하거나 다시 만들지 않았다. 개별 보존 판정과 원래 blob을 coverage에서 확인할 수 있다. 퇴역 `generate_scriptbinder_docs.py`는 현재 API 생성기로 안내하지 않으며, 입력/출력 한계는 [웹 문서 안내](WEBSITE.md)에 기록했다.

기존 문서 경로는 유지했다. dashboard의 기존 `phase-*` 주소는 원래 주제를 찾아가는 인덱스로 유지하며, 예전 진행률·추정 공수·완료 선언을 이어받지 않는다. 선별 API의 31개 page ID도 유지한다. 삭제된 멤버나 절의 14개 주소는 `legacyAnchors`를 통해 현행 계약으로 이동하고, 이전 주소였다는 안내를 표시한다. 폐기된 API의 선언을 살아 있는 것처럼 복원하지 않는다.

## 정적 검사와 실행 한계

저장소 루트에서 다음 읽기 전용 검사를 실행할 수 있다.

```sh
python docs/check_documentation.py
```

검사는 로컬 문서·들어오는 문서 링크, Markdown/HTML anchor, API page/card/소스/멤버 anchor, 호환 주소 대상, 소스 및 역사 screenshot hash, 전체 파일 coverage와 보존 판정을 확인한다. 인터넷 링크의 HTTP 응답, Markdown 플랫폼별 세부 렌더링, 코드 예제 컴파일이나 문서 의미를 자동 검증하지 않는다.

최종 로컬 정적 검사에서 링크 2,969개와 anchor 69개를 확인했고 오류는 0개였다. `git diff --check`도 통과했다. 이 결과는 아래 실행 검증과 구분한다.

이번 작업에서 엔진 빌드, Windows Editor/Player 실행, GPU 성능·화질 측정, package smoke, Vite build, 웹 브라우저/Pages 배포 검증은 수행하지 않았다. PR의 정적 검사 결과와 기존 역사 자료를 이 실행 검증의 대체 증거로 취급하지 않는다.


## 2026-10-17 PR #174 충돌 통합

master `b767a3c67e0f8474792a0a141a4580c5eac15712`를 병합했다. 5개 충돌 문서 중 LiveRenderWiring은 최신 master 내용을 보존하고, 비교·그림자 계획에는 후속 추가 기록을 이식했다. dashboard는 새 문서 인덱스를 유지하며 master 추가 문서로 연결한다. TexturePipelinePlan에는 후속 21~37절을 보존했다. master가 추가한 17개 문서는 원문을 보존하며 coverage의 merge_integration에 출처와 blob을 별도 기록한다.

원래 403파일 감사와 2026-10-15 소스 검토의 기준 커밋은 바꾸지 않았다. 이후 master의 소스 변경은 이 감사의 의미 검토를 받은 것으로 표시하지 않는다. 따라서 정적 검사에서 evidence-hash-drift/API source drift가 나오면 이전 기준과 현재 소스의 차이이며 전체 문서 재감사나 엔진 회귀 실행을 대신하지 않는다. master에서 승계한 Build/Artifacts 상대 링크는 로컬 실행 산출물이 없으면 missing-file로 보고된다. 이번 통합은 이 링크의 원본 로그나 측정 결과를 생성하지 않았다.

병합 결과 검사: 원본 감사 406파일과 master 추가 17파일로 문서는 423개다. 링크 3,011개·anchor 84개를 확인했다. 충돌 marker, 문서/승계 문서 hash 불일치, 문서 밖 master 대비 변경은 없다. 정적 검사 전체는 통과가 아니다. master에서 승계한 로컬 Build/Artifacts 링크 171개가 누락이며, 소스 drift는 고유 425경로(일반 근거 1,295건·API source 22건·API 근거 42건)다. 누락 링크는 모두 master 원문에 있고 drift 소스는 모두 master와 동일함을 별도로 대조했다. Windows UTF-8·POSIX 상대경로·Git 줄바꿈 정규화를 검사기에 반영했다. 최종 index 기준 무결성 검사와 git diff --cached --check는 통과했다. 엔진 빌드·실행 검증은 수행하지 않았다.
