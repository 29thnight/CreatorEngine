# PC 공용 프로파일 타임라인

`Tools/profiling-validation/Start-TimelineViewer.ps1`을 실행한 뒤 출력된 로컬 주소를 연다.
서버는 저장소를 루트로 사용하고 `127.0.0.1:8765`에서만 수신한다.

`index.html`과 `viewer.js`는 모든 측정에서 재사용한다. 측정마다 전체 타임라인 HTML을 생성하지 않는다.
CPU·GPU 원본이 같은 SHA256인지 확인하고, 선택한 데이터의 JSON만 읽는다.
상단 측정 선택은 `Artifacts/timeline-viewer/catalog.json`의 `measurements` 배열을 사용한다.
서버 없이 볼 때는 JSON 폴더 선택으로 측정의 `timeline` 폴더를 연다.

측정별 `timeline/viewer.json` 형식:

```json
{
  "schema": 1,
  "title": "측정 이름",
  "report": "../index.html",
  "datasets": [
    {
      "id": "on-a-etw",
      "variant": "on-a",
      "mode": "etw",
      "label": "재사용 ON · A · ETW",
      "cpu": "on-a-etw.json",
      "gpu": "on-a-etw-gpu.json"
    }
  ]
}
```

경로는 manifest 기준이다. 보고서만 측정별로 작성한다. CPU·GPU·ETW 전체 뷰어의 마크업은 수정하지 않는다.
뷰어는 PC 화면을 기준으로 작성했다.

JSON 생성 도구:

1. `export-timeline-native.py --root <측정 폴더> <실행 이름...>`: CPU·GPU 타임라인과 전체 CEPROF 기록 JSON.
2. `export-timeline-etw-states.py --root <측정 폴더> <ETW 실행 이름...>`: kernel CSV의 Running·Ready·Waiting 상태.
3. `export-timeline-raw.py --root <측정 폴더> <실행 이름>`: 원본 이벤트·조건·파생 상태 manifest. ETW가 있으면 필터 없는 xperf 행을 보존하고 JSON 왕복 바이트 해시를 확인한다.

컨테이너 분리 비교는 시작 시 `CREATOR_RENDER_CONTAINER_REUSE=0/1`을 설정한다. 기본값은 1이다.
0은 장벽 계획 내부 배열·재질 묶음 풀·기록 작업 그래프 저장소 보존을 끈다.
노드 선언 실행 방식, RHI/GPU 수명, 기록 스코프는 동일하다.
`RenderContainerReuse.On/Off` 이벤트로 실제 실행 정책을 확인한다.

공용 뷰어의 실제 소스와 실행 파일은 `Artifacts/timeline-viewer`에 둔다. 이전 Tools 경로는 주소 호환용 이동 페이지만 남긴다.

중복 심볼 정리 기록: `Artifacts/artifact-maintenance-20261022/pdb-references.json`. 이전 스냅샷의 심볼 경로는 `Tools/profiling-validation/Resolve-ProfilingSymbolPath.ps1 <BinarySnapshot 폴더>`로 해석한다. 보존된 PDB 디렉토리들이 세미콜론으로 출력된다.
