# 엔진 명령 표면 도표 — 2026-09-12 갱신

> 2026-09-12: 닫힌·완료 계획 소속 Commandlet 68개를 은퇴시켰다(§5). 제품 명령은 하나도 건드리지 않았다 — 아래 제품 수치는 2026-09-06 기준 그대로다.

현재 작업 트리의 시드와 등록, 런타임 registry를 대조한 표다. 제품 작업은 공통 편집·진단 API로, 검증은 프로세스 범위 Commandlet으로 실행한다.
[구조 및 검증](CommandSurfaceImplementation.md) · [처분표](CommandSurfaceDisposition.tsv) · [종결 검토](Phase14_5Closure.md)

## 1. 실행 표면

| 표면 | 정식 명령 | 별칭 포함 | 실행 경로 |
|---|--:|--:|---|
| Editor 제품 | 99 | 106 | console·batch·HTTP/JSON; `wait`는 HTTP 실행 제외 |
| Player 제품 | 7 | 7 | Development Player registry; 전용 5개 + 공통 `help`·`quit` |
| 등록형 Commandlet | 45 | 45 | `--commandlet` / `--commandlet-script` |
| 독립 Commandlet | 3 | 3 | 같은 실행 모드, 별도 진입점 표 |

Editor 제품과 Commandlet 48개의 이름 집합은 겹치지 않는다. 지금까지 제거한 이름은 82개다. MCP 서버는 후속 범위이며 HTTP discovery와 결과를 연결하는 어댑터로 추가한다.

```mermaid
flowchart LR
    GUI[GUI] --> Shared[공통 편집·진단 API / Undo]
    MCP[향후 MCP 어댑터] -.-> HTTP[HTTP/JSON 제품 명령]
    CLI[CLI] --> HTTP
    HTTP --> Shared
    Harness[Commandlet 실행 모드] --> Verify[독립 검증 / 제품 API 검사]
```

## 2. Editor 제품 명령 — 99개

모든 제품 명령이 `CommandResult`를 반환한다. JSON `parameters` 지원 59개, Undo 선언 21개다. `()`는 인자 없는 스키마다. 빈 칸은 positional `args` 사용이다. 동작에 따라 인자 구성이 달라지는 명령에 허위 스키마를 만들지 않았다.

`Immediate`·`Frames`·`Long`은 스케줄링 분류이며 실행 시간 보장이 아니다. Undo 선언은 편집 작업에만 적용된다. 저장·리로드·관리 코드 실행 등 서비스 작업은 별도 수명과 실패 결과를 갖는다.

### ai — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `ai.status` | - | `[오브젝트]` | Immediate | — | — | AI 레지스트리 등록 수를 낸다(오브젝트를 주면 그 하나) |

### animator — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `animator.param` | - | `<오브젝트> <파라미터> <bool\|float\|int\|trigger>` | Frames | ● | `target,name,type` | Animator 파라미터를 저작한다 |
| `animator.status` | - | — | Frames | — | `()` | Read live Animator palettes and product publication metrics |

### assets — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `assets.modeldiag` | - | — | Immediate | — | `()` | 모델 소비 계수를 읽는다(상태를 바꾸지 않는다) |
| `assets.unload` | - | — | Frames | — | `()` | 사용하지 않는 에셋 캐시 정리 |

### bt — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `bt.status` | bt.reset | — | Immediate | — | `()` | 행동 트리 지표(트리 수·틱 누계·프레임당 경계 통과) |

### camera — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `camera.editor` | - | `match \| follow [on\|off] \| status` | Frames | — | — | 에디터 카메라를 게임 카메라와 같은 시점으로 |

### cli — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `cli.drain.budget` | - | `[<시간ms> <개수>]` | Immediate | — | — | 서비스 큐 드레인 예산을 읽거나 바꾼다(LC5 · SLO 게이트의 변이용) |
| `cli.echo.args` | - | `<인자>` | Immediate | — | — | tokenizer가 만든 토큰을 길이와 함께 되비춘다(LC0 parser golden) |

### commands — 3

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `commands.describe` | - | `<이름>` | Immediate | — | — | 명령 하나의 descriptor 상세를 낸다 |
| `commands.list` | - | `[경로]` | Immediate | — | — | 등록 명령 snapshot 을 TSV 로 낸다(소스 스크래핑 대체) |
| `commands.selftest` | - | — | Immediate | — | — | registry 무결성을 판정한다(이름 중복·요약 누락·descriptor 부재) |

### component — 3

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `component.add` | - | `<오브젝트 이름> <컴포넌트 타입>` | Frames | ● | `target,type` | 오브젝트에 컴포넌트를 붙인다 |
| `component.list` | - | `[filter]` | Immediate | — | `filter=` | 등록된 컴포넌트 타입을 로그에 남긴다 |
| `component.remove` | - | `<target> <component>` | Frames | ● | `target,component` | Remove an optional component with Undo |

### crash — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `crash.status` | - | — | Immediate | — | `()` | 크래시 덤프 기록자 등록 여부와 덤프 경로를 확인한다 |

### dump — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `dump.list` | - | `[limit]` | Immediate | — | `limit:integer=10` | 크래시 덤프 목록을 최대 limit개 조회한다 |
| `dump.show` | - | `[limit]` | Immediate | — | `limit:integer=10` | 덤프 목록과 최신 덤프 요약을 조회한다 |

### dx12 — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `dx12.live` | - | `on\|status` | Immediate | — | `action=status` | EnhancedRenderer 메인 런타임 상태 |

### game — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `game.pak` | - | — | Long | — | — | Release Player 패키지를 빌드·검증 후 Build/Staging에 게시한다 |

### gc — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `gc.collect` | - | — | Frames | — | `()` | 관리 힙 확정 수집(씬 전환이 자동으로 부르는 그 경로) |
| `gc.stats` | gc.delta | — | Immediate | — | `label=` | 관리 힙 지표를 낸다(gc.delta는 직전 대비 증감) |

### gpu — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `gpu.baseline` | - | — | Frames | — | `()` | 현재 상태를 기준선으로 삼는다 |
| `gpu.census` | gpu.delta | `[라벨]` | Frames | — | `label=` | VRAM과 엔진 에셋 수를 로그에 기록 |

### help — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `help` | - | `[명령]` | Immediate | — | — | 명령 목록 또는 명령 하나의 상세를 낸다 |

### lifecycle — 3

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `lifecycle.dump` | - | `[파일]` | Immediate | — | `path=lifecycle_trace.tsv` | 기록을 TSV로 쓴다(기록 0건이면 실패로 끝난다) |
| `lifecycle.registry` | - | — | Frames | — | `()` | 생명주기 등록 수와 대기 중인 초기화 수를 조회한다 |
| `lifecycle.trace` | - | `on [틱프레임]\|off\|clear\|status` | Frames | — | — | 생명주기 호출 순서를 받아 적는다 |

### light — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `light.proxy` | - | — | Frames | — | `()` | Read live light proxy values and publication counters |

### log — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `log.flush` | - | — | Immediate | — | `()` | 로그를 디스크에 즉시 반영 |

### mem — 4

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `mem.delta` | - | `[라벨]` | Immediate | — | `label=` | 기준선 대비 CRT 블록·바이트 증감(기준선이 없으면 지금을 기준선으로 삼는다) |
| `mem.hook` | - | `on\|stack\|off\|top\|status` | Immediate | — | — | CRT 할당 훅 — 호출 계수, stack 은 귀속까지(디버그 CRT 전용) |
| `mem.reset` | - | — | Immediate | — | `()` | churn 누계와 기준선을 0으로 — 구간 측정용 |
| `mem.stats` | - | `[라벨]` | Immediate | — | `label=` | CRT 현재 live 블록·바이트를 찍는다(누계는 mem.hook 이 따로 센다) |

### model — 3

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `model.load` | - | `<경로>` | Long | — | `path` | 모델을 에셋으로 임포트한다(fbx/gltf/glb/obj) |
| `model.loadcached` | - | `<모델 경로>` | Long | — | `path` | 에디터 드롭 경로(LoadCachedModelShared)로 모델을 연다 |
| `model.place` | - | `<이름>` | Frames | ● | `model` | 임포트한 모델을 활성 씬에 배치한다 |

### object — 9

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `object.create` | - | `<이름> [타입]` | Frames | ● | `name,type=Empty` | 빈 오브젝트를 만든다(Empty/Light/Camera/Mesh) |
| `object.delete` | - | `<target>` | Frames | ● | `target` | Delete an object subtree with Undo |
| `object.describe` | - | `<name-or-id>` | Immediate | — | `target` | Read object identity and name |
| `object.duplicate` | - | `<오브젝트> [새 이름]` | Frames | ● | `target,name=` | 오브젝트를 복제한다(에디터 Ctrl+D와 같은 원시 함수) |
| `object.parent` | - | `<자식> <부모 \| ->` | Frames | ● | `target,parent` | 오브젝트의 부모를 바꾼다(-는 씬 루트로 올린다) |
| `object.properties` | - | `<target> <component>` | Immediate | — | `target,component` | Read reflected component fields and values |
| `object.property` | - | `<오브젝트> <컴포넌트> <필드> <값>` | Frames | ● | `target,component,field,value:value` | 리플렉션으로 프로퍼티를 설정한다 |
| `object.rename` | - | `<name-or-id> <new-name>` | Immediate | ● | `target,name` | Rename through the shared editor undo transaction |
| `object.transform` | - | `<이름> <px py pz> [rx ry rz] [sx sy sz]` | Frames | ● | `target,position:vec3,rotation:vec3=0 0 0,scale:vec3=1 1 1` | 변환을 지정한다(회전은 도) |

### pipeline — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `pipeline.nodes` | - | — | Immediate | — | `()` | 라이브 파이프라인의 노드 조립 결과를 한 줄씩 낸다 |

### pix — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `pix.capture` | - | `begin\|end\|status` | Frames | — | `action=status` | PIX 주입 실행의 명시적 GPU 캡처 경계 |

### play — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `play` | stop | — | Frames | — | `()` | 에디터의 재생·정지와 같은 동작 |
| `play.state` | - | — | Immediate | — | — | 재생 상태(gameStart·paused·씬 로드)를 낸다 |

### prefab — 6

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `prefab.create` | - | `<오브젝트 이름> <프리팹 이름>` | Frames | — | — | 오브젝트로 프리팹을 만들어 저장한다 |
| `prefab.instantiate` | - | `<프리팹 이름> [인스턴스 이름]` | Frames | ● | `prefab,name=` | 프리팹을 씬에 소환한다 |
| `prefab.objectguid` | - | `<오브젝트 이름>` | Immediate | — | — | 오브젝트의 프리팹 objectGuid를 낸다 |
| `prefab.overrides` | - | `<오브젝트>` | Immediate | — | — | 프리팹 인스턴스에 기록된 오버라이드를 나열한다 |
| `prefab.status` | - | — | Immediate | — | — | 프리팹 등록·캐시 상태를 낸다 |
| `prefab.update` | - | `<소스 오브젝트> <프리팹 이름>` | Frames | — | — | 기존 프리팹을 소스 오브젝트로 갱신한다 |

### profile — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `profile.stats` | - | — | Immediate | — | `()` | 프로파일러 자체 비용과 용량 소진(교란 없음) |

### quit — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `quit` | exit | — | Immediate | — | — | 호스트를 종료한다 |

### render — 4

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `render.backend` | - | `status` | Immediate | — | `action=status` | 부팅 시 고정된 scene/ImGui RHI 조회(변경은 Settings) |
| `render.matmode` | - | `<오브젝트> <opaque\|transparent>` | Immediate | ● | `target,mode` | 오브젝트 재질의 렌더링 모드를 바꾼다 |
| `render.rtinfo` | - | — | Immediate | — | `()` | 창·뷰포트·추종 텍스처 크기를 나란히 찍는다 |
| `render.shadowinfo` | - | — | Immediate | — | `()` | 그림자 캐스케이드 계산 결과를 출력한다(스냅샷 검증용) |

### scene — 15

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `scene.bonedump` | - | `[개수]` | Frames | — | — | 대조 덤프 — 뼈 오브젝트 이름 vs 스켈레톤 뼈 이름(조회 실패 진단) |
| `scene.ddol` | - | `<이름>` | Frames | — | — | 오브젝트를 DontDestroyOnLoad로 — 씬 이송 경로 시험용 |
| `scene.dump` | - | `[라벨]` | Immediate | — | — | 활성 씬의 오브젝트 계층을 로그에 남긴다 |
| `scene.flag` | - | `[<dirtytraversal\|bonecache> [0\|1]]` | Immediate | — | — | 씬 진단 플래그를 읽거나 바꾼다(인자 없으면 전부 조회) |
| `scene.hierarchycheck` | - | — | Frames | — | `()` | 씬 계층의 불변식을 전수 점검한다(고아·쌍불일치·순회미도달) |
| `scene.load` | scene.switch | `<경로>` | Long | — | — | 씬을 로드한다(활성 씬은 그대로) |
| `scene.new` | - | `[이름]` | Frames | — | — | 빈 씬을 만들어 활성화한다(기능 테스트 씬 저작용) |
| `scene.save` | - | `<경로>` | Frames | — | — | 활성 씬을 .creator로 저장한다 |
| `scene.select` | - | `<오브젝트 이름>` | Immediate | ● | `target` | 오브젝트를 에디터 선택으로 지정한다 |
| `scene.selection` | - | `<라벨>` | Immediate | — | — | 단일 선택과 복수 선택을 따로 낸다(둘의 어긋남을 드러낸다) |
| `scene.sparseresolver` | - | `0\|1\|print` | Frames | — | — | X5 dirty-root sparse resolve·A/B 검사 |
| `scene.transformdigest` | - | `[라벨]` | Frames | — | — | 활성 씬 전체의 트랜스폼 값 다이제스트(저장·재로드 대조용) |
| `scene.transformpull` | - | `[print]` | Frames | — | — | X6 C# 즉시 pull 계측 스냅샷을 조회한다 |
| `scene.transformstats` | - | `[0\|1\|print]` | Frames | — | — | X0 UI/Spatial·단계·구성·프레임 topology 계측 |
| `scene.transformwritestats` | - | `[0\|1\|print]` | Frames | — | — | X1 로컬 쓰기 publish 출처 계측 |

### script — 6

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `script.add` | - | `<오브젝트> <타입>` | Frames | — | — | C# 스크립트를 오브젝트에 부착한다 |
| `script.fields` | - | `<id>` | Frames | — | `instance:integer` | 스크립트의 노출 필드와 현재 값을 확인한다 |
| `script.invoke` | - | `<타입> <메서드> [인자]...` | Long | — | — | 표식된 static 메서드를 호출한다([EngineCallable] 없는 것은 거부) |
| `script.reload` | - | — | Frames | — | — | 게임 스크립트 어셈블리를 다시 로드한다(핫리로드) |
| `script.set` | - | `<id> <인덱스> <값>` | Frames | — | `instance:integer,index:integer,value` | 노출 필드 값을 바꾼다 |
| `script.status` | - | — | Immediate | — | — | CLR 상태와 활성 스크립트 수를 확인한다 |

### tag — 4

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `tag.add` | - | `<name>` | Immediate | ● | `name` | Add and persist a project tag with Undo |
| `tag.has` | - | `<name>` | Immediate | — | `name` | Query a project tag |
| `tag.list` | - | — | Immediate | — | `()` | Read project tags and layers |
| `tag.remove` | - | `<name>` | Immediate | ● | `name` | Remove and persist a project tag with Undo |

### ui — 7

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `ui.anchor` | - | `<오브젝트> <minX> <minY> <maxX> <maxY>` | Frames | ● | `target,minX:number,minY:number,maxX:number,maxY:number` | 앵커를 직접 지정한다 |
| `ui.hitbox` | - | — | Frames | — | — | 버튼의 rect와 클릭 판정 상자를 나란히 출력한다 |
| `ui.pos` | - | `<target> <x> <y>` | Frames | ● | `target,x:number,y:number` | UI anchored position을 편집한다 |
| `ui.rect` | - | `<오브젝트\|*>` | Frames | — | — | 오브젝트 이하의 worldRect·sizeDelta·앵커·배율을 출력한다 |
| `ui.screenpos` | - | `<target> <x> <y>` | Frames | ● | `target,x:number,y:number` | UI 화면 위치를 편집한다 |
| `ui.size` | - | `<target> <x> <y>` | Frames | ● | `target,x:number,y:number` | UI 크기를 편집한다 |
| `ui.status` | - | — | Immediate | — | — | UI 계층·캔버스 연결 상태를 낸다 |

### undo — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `undo` | redo | — | Frames | ● | `()` | 에디터의 Ctrl+Z / Ctrl+Y와 같은 호출 |
| `undo.state` | - | `<라벨>` | Immediate | — | — | 편집 스택과 게임 스택의 Undo 깊이를 따로 낸다 |

### wait — 1

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `wait` | - | `<프레임>` | Immediate | — | — | 지정 프레임만큼 다음 명령을 미룬다 |

### window — 2

| 명령 | 별칭 | 인자 | 비용 | Undo | JSON parameters | 동작 |
|---|---|---|---|:-:|---|---|
| `window.info` | - | — | Immediate | — | `()` | 엔진이 인식하는 클라이언트 크기를 출력한다 |
| `window.resize` | - | `<너비> <높이>` | Frames | — | `width:integer,height:integer` | 창 클라이언트 크기를 바꾼다(해상도 검증용) |

## 3. Commandlet — 48개

검증 완료 후 역할이 끝난 일회성 하네스는 제거한다. 현재 제품 계약을 지키는 회귀·측정 Commandlet은 유지한다. 코퍼스 요구사항과 실제 실행 성공은 별개다. 모든 정상 반환 경로는 terminal 결과를 내며, `crash.test`의 유효한 입력은 덤프 검증을 위해 프로세스를 의도적으로 종료한다.

| 명령 | 인자 | 비용 | 검증 |
|---|---|---|---|
| `assets.decodeab` | `[root] [limit]` | Long | Compare PNG decoder bytes |
| `assets.decodeabhdr` | `[root]` | Long | Compare HDR decoder values |
| `assets.scenemodel` | `[reload <모델 이름>]` | Frames | 활성 씬의 모델 소비가 typed generation handle로 서 있는지 본다 |
| `assets.texturebench` | `[limit]` | Long | Measure texture decode mip and compression stages |
| `blackboard.authoring.probe` | `<이름> [empty\|noname]` | Frames | Blackboard 저장·재로드 왕복으로 키 값이 살아 돌아오는지 본다 |
| `collisionmatrix.authoring.probe` | `[escape]` | Frames | 충돌 행렬 저장·재로드 왕복과 설정 루트 이탈 거부를 본다 |
| `crash.test` | `[av\|abort\|terminate\|throw]` | Immediate | 의도적인 프로세스 종료로 덤프 경로를 검증한다 |
| `dx12.forward` | — | Long | DX12 forward 패스를 격리 씬에서 리드백으로 판정한다 |
| `dx12.forwardshade` | — | Long | DX12 forward 셰이딩 결과를 리드백으로 판정한다 |
| `dx12.gbuffer` | — | Frames | GBuffer 패스 검증(입력조립·MRT5·깊이·그래프 배리어) |
| `dx12.gizmoscene` | — | Frames | Gizmo 씬 연결 검증(밀봉 복사·4패스 체인·타깃 공유) |
| `dx12.post` | — | Frames | DX12 후처리 패스를 리드백으로 판정한다 |
| `dx12.scene` | — | Long | 씬 연결 검증(카메라 스냅샷·메시 업로드·실제 드로우) |
| `dx12.selftest` | `<texture-path> [output]` | Frames | DX12 브링업 자가 검증(삼각형 렌더 → PNG) |
| `dx12.shadowquality` | — | Frames | 그림자 품질 검증(경사 비례 편향·캐스케이드 경계 블렌딩 A/B) |
| `dx12.skinning` | — | Frames | GBuffer 스키닝 검증(본 이동·가중 혼합·비스킨드 불변) |
| `dx12.ui` | — | Frames | DX12 UI 패스를 리드백으로 판정한다 |
| `experiment.cooked` | `[경로]` | Long | 쿠킹 포맷 왕복 무손실·거부 동작(경로를 주면 실자산 왕복까지) |
| `foliage.authoring.probe` | `<이름> [escape]` | Frames | Foliage 저작 트랜잭션 왕복과 루트 이탈 거부를 본다 |
| `inputmap.authoring.probe` | `<save\|verify> <이름>` | Frames | 입력 액션맵 저장·재기동 왕복으로 payload 복원을 본다 |
| `inputmap.corpus.probe` | — | Long | 입력 액션맵 코퍼스를 전수로 읽어 계수를 낸다 |
| `material.corpus.probe` | `<이름> ...` | Long | standalone material identity/reference 왕복 |
| `object.rootref` | `<오브젝트> [루트\|-]` | Frames | Bone형 same-scene root 참조를 설정/조회한다 |
| `prefab.corpus.digest` | `<라벨> <이름> ...` | Long | prefab identity/override 왕복 digest |
| `profile.selftest` | — | Frames | CPU 프로파일러 특성화 검사(중첩·멀티스레드·프레임경계·용량초과) |
| `render.livecheck` | `[너비 높이]` | Immediate | resize·다중 뷰·표시 슬롯 회전 회귀 판정 |
| `shadermeta.probe` | — | Frames | ShaderMeta 실자산 수용과 잘못된 문서 거절을 함께 판정한다 |
| `terrain.authoring.probe` | `<이름> <텍스처\|->` | Frames | Terrain writer 트랜잭션 회귀 검사 |
| `ui.navprobe` | — | Frames | UI 내비게이션 저작 계층을 세워 탐색 결과를 판정한다 |
| `vk.decal` | — | Long | Decal 공용 패스 — GBuffer snapshot·depth-read·MRT blend 대조 |
| `vk.deferred` | — | Frames | Deferred 공용 패스 — GBuffer consume·fullscreen DX12/Vulkan 대조 |
| `vk.forward` | — | Frames | Forward+ 공용 패스 — compute·buffer·blend·mesh DX12/Vulkan 대조 |
| `vk.gbuffer` | — | Long | GBuffer 공용 패스 — MRT5·texture·sampler·mesh DX12/Vulkan 대조 |
| `vk.grid` | — | Frames | 그리드 패스를 Vulkan 으로 — dx12.grid 와 픽셀 대조(5d) |
| `vk.shadow` | — | Frames | Shadow 공용 패스 — depth array·mesh DX12/Vulkan 대조 |
| `vk.texturecodec` | — | Frames | Validate neutral texture bytes across DX12 and Vulkan |
| `experiment.matmigrate` | — | 독립 진입점 | 합성 검사와 실제 제품 경로 검사 |
| `experiment.matresolve` | — | 독립 진입점 | 합성 검사와 실제 제품 경로 검사 |
| `experiment.matscript` | — | 독립 진입점 | 합성 검사와 실제 제품 경로 검사 |

## 4. Player 제품 — 7개

Shipping에서는 명령 서비스·소켓이 빌드에서 제외된다. 아래는 Development 제공 범위다.

| 명령 | 인자 | 동작 |
|---|---|---|
| `help` | `[명령]` | 명령 목록 또는 명령 하나의 상세를 낸다 |
| `player.move` | `<이름> <x> <y> <z>` | 오브젝트의 로컬 위치를 옮긴다(재시작 없이 반영된다) |
| `player.object` | `<이름>` | 오브젝트 하나의 위치·회전·크기를 낸다 |
| `player.objects` | `[이름 조각]` | 활성 씬의 오브젝트 이름을 나열한다 |
| `player.scene` | — | 활성 씬 이름과 오브젝트 수를 낸다 |
| `player.status` | — | 프레임 수·재생 상태·명령 큐 깊이를 낸다 |
| `quit` | — | 호스트를 종료한다 |

## 5. 제거 및 분리

| 이름 | 정리 결과 |
|---|---|
| `cli.probe.timing` | 종료된 LC0 지연 계측 제거; HTTP 요청 timing 사용 |
| `commands.dump` | 종료된 등록 스냅샷 제거; `commands.list` 사용 |
| `dx12.bench11` | 완료된 DX11/DX12 비용 비교와 전용 구현 제거 |
| `dx12.encoderbench` | 완료된 encoder 선택 벤치와 전용 구현 제거 |
| `dx12.forwardscale` | 광원 수별 시간 비교 제거; `dx12.forwardshade` 픽셀 비교 유지 |
| `dx12.postscale` | Uber/분리 패스 시간 비교 제거; `vk.post` 픽셀 비교 유지 |
| `dx12.ssaoscale` | 시간 비교 및 제품 SSAO의 옛 참조 PSO·셰이더 제거 |
| `experiment.animlive` | 제품 진단 `animator.status`로 이전 |
| `experiment.matparity` | 폐기된 legacy material packer의 종료된 대조 검사 제거 |
| `perf.reflect` | 종료된 CT7 측정 및 소비자의 측정 전용 준비 제거; `reflect.golden` 유지 |
| `selftest` | 통합 검증 진입점 제거; 별도 Commandlet 실행 경로 사용 |
| `tag.authoring.probe` | 제품 `tag.list/has/add/remove`로 이전 |
| `animator.state` | 사용하지 않는 상태·스크립트 강제 실행 하네스 제거; 정상 Animator 전이 경로 유지 |
| `animator.exit` | 사용하지 않는 강제 상태 종료 하네스 제거 |

### 닫힌·완료 계획 소속 은퇴 — 2026-09-12 · 68종

기준은 둘을 함께 충족하는 것이다: (1) 그 Commandlet 을 낳은 계획이 [보관함](../plans/archive/README.md) 에 들어갔거나 PHASE 17 처럼 폐쇄됐고, (2) 활성 계획 문서가 그 이름을 더 이상 부르지 않는다.
활성 계획이 아직 부르는 Commandlet 은 같은 보관함 계획 소속이어도 남겨 두었다.

| 은퇴한 이름 |
|---|
| `animator.scene.probe` |
| `asset.guid.rename.probe` |
| `assets.generation` |
| `assets.generationcorpus` |
| `assets.identity` |
| `assets.modelbench` |
| `assets.modelrender` |
| `assets.sidecar` |
| `dx12.decal` |
| `dx12.descriptorheap` |
| `dx12.fog` |
| `dx12.gizmoicon` |
| `dx12.gizmoline` |
| `dx12.grid` |
| `dx12.ibl` |
| `dx12.iblshade` |
| `dx12.parallel` |
| `dx12.psocache` |
| `dx12.rendergraph` |
| `dx12.resize` |
| `dx12.skybox` |
| `dx12.ssao` |
| `dx12.ssgi` |
| `dx12.ssr` |
| `dx12.sss` |
| `dx12.wireframe` |
| `experiment.animevent` |
| `experiment.animmask` |
| `experiment.animpose` |
| `experiment.animtick` |
| `experiment.boneresolve` |
| `experiment.catalog` |
| `experiment.editorsurface` |
| `experiment.foliage` |
| `experiment.matcook` |
| `experiment.matruntime` |
| `experiment.scenecook` |
| `experiment.skinbounds` |
| `lifecycle.stress` |
| `reflect.golden` |
| `rhi.uploadsegments` |
| `scene.executiongraph` |
| `scene.hierarchymutation` |
| `scene.proxybench` |
| `scene.proxydirty` |
| `scene.sparseresolver.check` |
| `scene.transformbulk` |
| `scene.transformdomains` |
| `scene.transformpull.check` |
| `scene.transformwritestats.check` |
| `scene.traversalbench` |
| `serialize.bench` |
| `serialize.nodeequal` |
| `serialize.rymlerror` |
| `vk.fog` |
| `vk.gizmoicon` |
| `vk.gizmoline` |
| `vk.ibl` |
| `vk.parallel` |
| `vk.post` |
| `vk.selftest` |
| `vk.skybox` |
| `vk.ssao` |
| `vk.ssgi` |
| `vk.ssr` |
| `vk.sss` |
| `vk.ui` |
| `vk.wireframe` |

은퇴와 함께 그것만 부르던 회귀 스크립트 27개와 전용 fixture 10개를 지웠고, run-all 에서 25칸이 빠졌다. Commandlet 을 불러 주던 C++ 자가검사 구현 41개는 이번 범위에 넣지 않았다 — 살아남은 검사가 같은 파일의 공용 호스트를 쓴다.

`assets.scenemodel`은 코퍼스 검사 및 reload 검증이므로 Commandlet으로 이동했다. `animator.param`과 `render.matmode`는 공통 GUI 편집 API 및 Undo 경로를 사용한다.

### 명시적 경로 입력 검사 — 1종(나머지 5종은 은퇴)

| 명령 | 현재 입력 | 유지한 검사 조건 |
|---|---|---|
| `dx12.selftest` | `<texture-path> [output]` | 입력 텍스처의 GUID·owner 및 shader/material/RHI 계약 |

명령 본문이 Gunner/SU/scene.glb/Cube 텍스처를 임의로 선택하지 않는다.
소비 스크립트의 코퍼스 기본값과 독립 기대값은 별개다. `verify-model-typed-consumers.ps1`에
다른 모델·씬을 지정할 때는 `ExpectedPoseDigest`도 명시해야 한다.

### 남아 있는 코퍼스 기준

| 대상 | 현재 등록·성격 | 의존성 |
|---|---|---|
| `assets.scenemodel` | **제품 등록에 잔존** | Gunner를 포함한 씬의 embedded texture 추가 단정; §6 참고 |
| 스킨 pose·coverage 소비 스크립트 | 전용 회귀 시나리오 | 지정 씬·모델 및 독립 pose/픽셀 기대값 |
| 내장 shader/meta fixture | 엔진 계약용 검사 입력 | 엔진의 기본 셰이더와 자가 검사 셰이더 |

코퍼스 파일 부재는 검사 통과나 기능 미구현의 증거가 아니다.







## 6. 결과 계약과 검증 경계

- 종료 상태: `succeeded`, `invalid_arguments`, `preconditions_failed`, `failed`, `cancelled`, `timed_out`, `internal_error`. 미보고 상태와 void 어댑터는 제거했다.
- 결과는 소유형 값이다. 프로파일러·GPU·파이프라인·메모리·스크립트 필드와 검증 계수는 측정 지점에서 직접 수집하며 로그 재파싱으로 만들지 않는다.
- 없는 CLR 인스턴스·모델·검사 코퍼스, 잘못된 숫자 및 지원하지 않는 동작은 실패 상태를 반환한다. Release에서 Debug CRT 지표를 0으로 보고하지 않는다.
- `assets.identity`는 예상 벡터 대신 실제 계산 UUID를 반환하여 독립 언어 대조를 유지한다.
- `dx12.selftest`의 material authoring round trip 실패와 누락된 canonical 코퍼스는 통신/명령 표면 정리 완료로 해결됐다고 간주하지 않는다. 최신 실행 판정은 [종결 검토](Phase14_5Closure.md)에 기록한다.
