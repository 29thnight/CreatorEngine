# 명령 registry 전면 재조사 — 제거·병합·undo·cost

작성 2026-09-05. 대상은 Editor registry의 **명령 212개 / 이름 226개**
(`Tools/regression/cli_registry.golden.tsv` 기준).

이 문서는 계획서가 아니라 **조사 기록**이다. 판정과 근거를 적고, 실행 순서는 §6에
후보로만 둔다. 무엇을 지울지는 이 문서가 정하지 않는다.

## 0. 왜 이 조사를 했나

두 가지 기준이 새로 섰다.

1. **에디터·개발에 필요한 조작만 명령으로 둔다.** 기본 동작의 조합으로 되는 것은
   별도 명령으로 만들지 않는다.
2. **mutating 명령은 undo/redo를 남긴다.** 남기도록 고친 뒤, 그러고 나면 존재 이유가
   사라지는 명령을 제거한다.

registry에 들어간 명령은 골든 행·descriptor·help 줄·서명 이행 대상이 되어 영구
유지 비용이 붙는다. PHASE 14.5 §18이 "모든 command가 terminal `CommandResult`를
만든다"를 요구하므로, 한 번 등록된 명령은 **폐기 조항 없이** 계속 따라온다.

## 1. 방법과 한계

- registry 스냅샷을 축(class·cost·roles·liveness·aliases)별로 집계하고, 도메인
  단위로 **핸들러 구현을 직접 읽었다.** 요약(summary) 문자열은 신뢰하지 않았다 —
  §3.1에서 보듯 요약이 실제 동작과 다른 자리가 있다.
- 소비자 참조는 `Tools/`·`scripts/`·`docs/`의 **텍스트 검색**이다. 사람이 손으로
  부르는 것은 보이지 않는다.
- 한 군데는 코드 추론을 **실측으로 뒤집었다**(§5). 나머지 판정은 코드 읽기다.

### 이 조사에서 틀렸다고 확인된 가설 셋

조사 전에 세운 것 중 셋이 코드와 맞지 않았다. 기록해 둔다 — 같은 추측을 다시 하지
않기 위해서다.

| 가설 | 실제 |
|---|---|
| `vk.X`는 `dx12.X`의 백엔드 파라미터이니 36개가 18개로 접힌다 | **아니다.** `vk.X`가 같은 프로세스에서 DX12 기준선을 세우고 두 백엔드를 픽셀 비교한다. §3.1 |
| `experiment.*` 미참조는 14개 | **18개.** `.txt` 시나리오를 읽는 `.ps1`이 없는 고아가 넷 더 있다. §3.2 |
| undoable 클래스는 5종 | **6종.** `Meta::CustomChangeCommand`가 빠져 있었고, GUI가 실제로 가장 많이 쓰는 것이 그것이다. §4 |

## 2. 지금 상태 — 축별 집계

| class | 전체 | 결과형 이행 | 미이행 |
|---|---:|---:|---:|
| `probe` | 126 | 72 | 54 |
| `engine_service` | 66 | 21 | 45 |
| `editor_operation` | 14 | 0 | 14 |
| `raw_fixture` | 6 | 1 | 5 |

| 축 | 분포 |
|---|---|
| `roles` | `editor` 210 · `both` 2 |
| `liveness` | `live` 206 · `terminates_process` 3 · `requires_restart` 3 |
| `cost` | `frames` 152 · `immediate` 35 · `long` 25 |

`probe`가 126개로 전체의 59%다. 이행한 94개 중 72개가 probe다 — **검증용 명령이
소수의 예외가 아니라 registry의 다수다.**

## 3. 축 A — 제거·병합

### 3.1 `dx12.*` 35 + `vk.*` 20 — 제거 가능한 것은 **1개**

가설이 깨진 자리다. 18개 패스 이름이 양쪽에 다 있지만, 그중 **14개에서 `vk.X`가
DX12 기준선을 같은 프로세스에 직접 세우고 두 백엔드를 픽셀 비교한다.** 즉

```
vk.ssao = f(dx12) ∧ f(vk) ∧ diff(둘)
```

`dx12.ssao`의 파라미터화가 아니라 **그것을 포함하는 합성**이다. `dx12.*`를 지워도
vk 쪽이 줄지 않고, `vk.*`를 지우면 저장소의 유일한 교차 백엔드 패리티 게이트가
사라진다. 임계값도 갈린다 — dx12는 `flatAO ≥ 0.9`, vk 하니스는 `> 0.85`.

"백엔드가 파라미터"라는 구조는 **이미 구현되어 있다. 한 단계 아래에서.**
`VulkanGeometryPassTest.cpp`의 `template <typename TResources>` capture 헬퍼 일곱이
DX12 자원 타입과 Vulkan 자원 타입으로 각각 인스턴스화된다. 파라미터화는 명령
층이 아니라 픽스처 층의 일이었다.

**제거 후보 1개**

| 명령 | 근거 |
|---|---|
| `dx12.uploadring` | seed에 스스로 "구 명령 별칭"이라 적혀 있는데 **별칭이 아니다** — `aliases` 칸이 `-`이고 자기 canonical 이름으로 따로 등록된다. `rhi.uploadsegments`의 **부분집합**(같은 `RunUploadSegmentTest`를 부르되 Vulkan 절반을 뺀 것). `liveness`도 어긋난다: 같은 DX12 작업인데 이쪽은 `Live`, 저쪽은 `RequiresRestart`. `Tools/dx12-validation/README.md`는 이것이 9회 중 1회 실패하는 비결정적 검사라 기준선에서 빼라고 적어 두었다 |

★ 이 짝은 **§3.4에서 반대편으로도 걸린다** — `rhi.uploadsegments`의 몸통이
`dx12.uploadring ∧ vk.selftest`라 "기본 동작의 조합"에 정확히 해당한다. 둘 중
어느 쪽을 남길지는 결정 사항이고, 이 문서는 양쪽 근거만 적는다.

**제거가 아니라 표기 오류 셋** — 요약이 실제 동작과 다르다.

| 명령 | 요약이 말하는 것 | 실제 |
|---|---|---|
| `dx12.ssaoscale` | 해상도 스케일 판정 | **벤치마크.** 옛 64샘플 커널 SSAO를 같은 디바이스에 올려 시간 비교 |
| `dx12.postscale` | 해상도 스케일 판정 | **벤치마크.** Uber post 대 같은 셰이더 4회 |
| `dx12.forwardscale` | 해상도 스케일 판정 | **벤치마크.** 광원 수 스윕. 기본 128×128에서는 돌 수 없다 |

셋 다 **다른 어디에도 없는 참조 구현**을 돌린다. 지우면 성능 주장의 유일한 증거가
사라진다. `cost`도 `Frames`인데 `Long`이어야 한다(§5).

**소비자 비대칭** — `dx12.*` 35개는 `Invoke-Dx12Suite.ps1`이 `commands.list`로
동적 전수 스윕한다. `vk.*` 20개는 소비자가 **하나**뿐이고
(`verify-model-render-wiring.ps1`) 그것이 3개만 문자열 리터럴로 부른다. **17개가
어떤 자동화에도 안 걸린다.** 게다가 그 하나가 stdout 정규식으로 판정을 읽는다 —
LC9가 dx12 쪽에서 없앤 바로 그 방식이다.

### 3.2 `experiment.*` 30 — 12개가 registry를 떠날 수 있다

구현이 `Editor/RenderTests/ExperimentParity/Experiment*SelfTest.cpp` 18개 파일,
8,277줄이다. **파일 이름이 답을 말한다** — 이것들은 에디터 조작이 아니라 유닛
테스트인데, 돌릴 러너가 없어 명령 registry를 빌려 입었다.

저장소에 이미 그 자리가 있다: `Tools/regression/*_contract_probe.cpp` +
`verify-*.ps1`. 선례 셋(`mathematics_contract_probe.cpp` 936줄 ·
`hashing_string_contract_probe.cpp` · `authoring_base64_contract_probe.cpp`).
`cl.exe`로 엔진 헤더에 직접 컴파일하고 명령 registry를 거치지 않는다.
**새로 만들 기전이 없다.**

| 계층 | 명령 | 줄수 |
|---|---|---:|
| **1** — 합성 전용, 엔진 표면 0 | cacheopt · matcodec · matinstance · matseal · resolver · sampler+tangent+normal · vertexlayout · weld | 2,908 |
| **2** — 실자산 부분도 순수 파일 I/O | smcook · texcook | 932 |
| | **12개** | **3,840** |

계층 1의 근거: 그 파일들에 `DataSystems`·`SceneManagers`·`GetActiveScene`·`ID3D12`·
`GameObject`가 **0회** 나온다. include가 순수 알고리즘 헤더뿐이다.

> ★ **이 근거는 절반만 맞았다 — 2026-09-05 실측이 정정한다.**
>
> include 표면과 **링크 폐포는 다른 문제다.** 자기 코드가 디바이스를 안 쓰더라도
> 링커는 참조한 심볼의 **정의**를 요구하고, 그 정의가 사는 `.cpp` 가 GPU·런타임
> 의존을 끌고 온다. 12개를 다 옮기려다 실측으로 갈렸다:
>
> | 명령 | 링크가 추가로 요구한 것 | 결과 |
> |---|---|---|
> | `weld` · `resolver` · `sampler` · `tangent` · `normal` | `Experiment/Import` 계열 `.cpp` 7개 + `mikktspace.c` | **이관 완료** |
> | `texcook` | `ReadAssetIdFromMeta` → `Authoring::ParsedDocument` → **rapidyaml 전체** | 보류 |
> | `cacheopt` · `vertexlayout` | `Mesh` · `MeshOptimizer` · `Core::TimeSystem` | 보류 |
> | `matcodec` · `matinstance` · `matseal` · `smcook` | `Material` · `Texture` · `Core::TimeSystem` | 보류 |
>
> 그래서 §6의 3번은 **12개가 아니라 5개**가 닫혔다. 나머지 7개는 "옮길 수 없다"가
> 아니라 "옮기려면 엔진 정적 라이브러리를 링크해야 하고, 그러면 기존 contract
> probe 셋의 성격(엔진 빌드 없이 도는 단독 실행)이 바뀐다" 이다. 그 결정을 이
> 조사가 대신 내리지 않는다.
>
> ★★ 컴파일은 **10개 전부 통과했다.** 문서가 위험으로 적었던 `matinstance`·
> `matseal` 의 include 표면(`Texture.h`·`ShaderMetaReflection.h`)은 문제가 아니었다 —
> `/external:W0` 로 엔진 헤더 경고를 빼고 `NOMINMAX` 를 주면 깨끗하다. 위험은
> 예상한 곳이 아니라 링크 단계에 있었다.

계층 2의 근거: `smcook`/`texcook`의 "실자산" 부분은 파일 읽기 + memcmp + SHA-256이고,
프로듀서가 `DataSystem.h`를 아예 include하지 않는다. 게다가 그 부분은 CLI 인자 둘을
요구하는데 **어떤 스크립트도 그 인자를 주지 않아 오늘 한 번도 안 돈다.**

`sampler`·`tangent`·`normal`은 **registry 항목 셋이 파일 하나(790줄)를 나눠 쓰고**
있다. 셋이 각자 18줄짜리 같은 래퍼를 단다.

**미참조가 18개다.** `.txt` 시나리오가 참조해도 **그 `.txt`를 읽는 `.ps1`이 없으면**
자동화가 돌지 않는다. 그런 고아가 넷(`animevent`·`matruntime`·`matcodec`·
`matmigrate`) 더 있다.

**anim 계열은 병합하면 안 된다.** 엔티티 필터와 판정 계약이 갈린다 —
`animpose`는 단정이 아니라 **뮤테이터**(`m_AnimIndexChosen`을 쓴다), `animevent`는
저장·재로드를 사이에 둔 2단계 상태 기계, `animlive`는 이전 호출과의 차이로만 의미가
생긴다. 합치면 인자 문법 셋과 판정 계약 둘이 한 dispatcher로 뭉갠다. 공통인 것은
6줄짜리 씬 획득 서두뿐이다.

배치는 소비자 쪽에 **이미 있다** — `verify-model-typed-consumers.ps1`이 한 세션에서
experiment 여섯을 몰아 돌린다. 배치는 거기 있을 일이지 registry에 있을 일이 아니다.

### 3.3 `scene.*` 23 — 순수 토글 둘, 슬라이스 분열 둘

**순수 토글은 넷이 아니라 둘이다.** `scene.dirtytraversal`과 `scene.bonecache`만
순수 get/set이고, 두 핸들러(33줄·31줄)는 접근자 쌍과 문자열 넷을 빼면 **줄 단위로
동일**하다. `transformstats`·`transformwritestats`·`sparseresolver`는 `0|1` **분기만**
중복이고 명령 자체는 실질 내용이 있다.

그 둘이 존재하는 이유가 `Scene.h` 주석에 적혀 있다 — "켠 값과 끈 값을
`scene.traversalbench 0 <프레임>`으로 각각 재서 비교하는 것이 이 슬라이스의 측정
방법이다". **`traversalbench`에 줄 수 없었던 인자**다. 증거가 `traversalbench` 안에
있다: 헤더에 `dirtytraversal=%s bonecache=%s`를 찍는다 — **자기가 못 켜는 플래그를
읽는다.**

**한 슬라이스가 명령 둘로 갈린 곳이 둘이다.**

| 슬라이스 | 갈라진 명령 | 같은 저장소의 관례 |
|---|---|---|
| X0 | `transformstats` + `traversalbench` | `executiongraph probe\|bench` |
| X8 | `proxydirty` + `proxybench` | `sparseresolver 0\|1\|print\|probe\|bench` |

나머지 슬라이스는 전부 "명령 하나, 모드가 첫 인자"를 따른다. 이 둘만 예외다.

**probe 8개는 병합 대상이 아니다.** 9단계 패턴이 8번 복붙되어 있고
`"probe 픽스처를 만들지 못했다(create=0)"`가 6곳에 똑같이 있지만, **단정하는
불변식이 전부 다르다**(X1 쓰기사유 · X2 UI/Spatial dirty · X3 `ReparentResult` ·
X4 packed projection · X5 sparse A/B · X6 C# pull · X7 배리어 · X8 proxy mask).
중복은 보일러플레이트지 판정이 아니다 — RAII `ProbeFixture` 헬퍼로 뽑을 일이지
명령을 줄일 일이 아니다.

`scene.selection`은 `scene.dump`에 selection 절이 없어서 생긴 30줄이다.
`scene.dump`와 `scene.transformdigest`는 데이터가 ~80% 겹치지만 **sink가 다르다**
(로그 대 stdout) — 억지로 합치지 않는다.

**소비자 0인 scene 명령 다섯**: `dirtytraversal` · `bonecache` · `bonedump` ·
`transformstats` · `traversalbench`.

### 3.4 나머지 84개 — 명확한 제거 후보 넷과 결함 하나

**결함 하나가 같이 나왔다.** `dump.crash`는 `crash.test`와 같은 네 분기를 갖고
(기본값만 `"access"` 대 `"av"`로 다르다) **호출자가 0**이다. 게이트는
`crash.test`를 쓴다(`verify-crash-dump.ps1:40`). 그런데 더 나쁜 것은,
`dump.crash`가 `reg.Escaping`이 아니라 **`reg.Legacy`로 등록되어 있다**는 것이다.
그래서 `dump.crash throw`가 `ConsoleCommandSystem.cpp:4320`에서 잡혀
`InternalError`로 바뀐다 — **프로세스가 살아남고 덤프가 안 써진다.** 자기
descriptor는 `terminates_process`라고 적혀 있다(`CommandDescriptorSeeds.cpp:64`).
일부러 죽어서 덤프 경로를 검증하는 것이 존재 이유인데, 죽지 않는다.

| 명령 | file:line | 실제 | 판정 |
|---|---|---|---|
| `dump.crash` | `DiagnosticsCommands.cpp:673` | `crash.test`와 같은 몸통, 호출자 0, **죽지 않는 crash 명령** | 제거 |
| `render.exposure` | `RenderDebugCommands.cpp:397` | **printf 한 줄뿐.** DX11 진단이 꺼졌다는 안내만 한다. 요약은 "무엇을 재고 무엇을 결정했는지"라고 적혀 있다 | 제거(이미 지운 `render.post`와 같은 형태) |
| `model.loadcached` | `AssetAuthoringCommands.cpp:904` | `model.load`의 후반부와 같은 한 줄(`LoadModelAssetGenerationByPath`). 게다가 **존재 이유를 적은 주석이 코드와 안 맞는다** — 드롭 경로가 `LoadCachedModelShared`를 쓴다고 적었는데 몸통은 그 함수를 부르지 않는다 | 제거 검토 |
| `window.info` | `CoreCommands.cpp:637` | `render.rtinfo` 첫 줄과 같은 두 값을 같은 버스에서 읽는다 | 병합 |

**합성 하나 — 규칙에 정확히 걸린다.** `rhi.uploadsegments`의 몸통은
`DX12Test::RunUploadSegmentTest` **그리고** `RunVulkanSelfTest`를 부르고 두 판정을
`&&`로 묶은 것이다. 각각 `dx12.uploadring`과 `vk.selftest`의 유일한 일이다.
**기본 동작 둘의 조합**이다.

다만 이 짝은 한쪽만 고를 수 있다. 규칙대로면 합성인 `rhi.uploadsegments`가 나가고
소비자가 둘을 각각 부른다. 반대로 `dx12.uploadring`은 자기 seed가 스스로
"구 명령 별칭"이라 적었고(그런데 별칭이 아니다) README가 9회 중 1회 실패하는
비결정적 검사라 기준선에서 빼라고 한다. **어느 쪽을 남길지는 결정 사항이다.**

**중복이 아니라고 확인된 것들** — 의심했지만 코드가 아니라고 답했다:
`crash.status`↔`dump.list` · `object.rootref`↔`object.property`(root index는
`Entity`에 있고 `Entity::reflect()`에 없어 반사로 못 닿는다) ·
`serialize.bench`↔`perf.reflect`(로드 대 쓰기, 반대 방향) ·
`profile.*`/`perf.*`/`pix.*`/`gpu.*`/`gc.*`/`mem.*`(일곱 개가 서로 다른 계수원) ·
`prefab.*` 넷 · `asset.guid.rename.probe`.

`profile.stats`는 출력이 `profile.selftest`에 통째로 포함되지만 **일부러 둘이다** —
`selftest`는 프레임 경계를 넘어 라이브 캡처를 교란하고 `stats`는 안 한다.

**`*.authoring.probe` 여섯은 제거 대상이 아니다.** 여섯 다 자기 자산 종류의
**유일한 CLI 창구**다(태그·충돌행렬·블랙보드·terrain·foliage·인풋맵). 지우면
중복이 아니라 기능이 사라진다. 다만 둘이 걸린다:

- **여섯 중 다섯이 만든 산출물을 치우지 않는다.** `collisionmatrix`만 복원한다 —
  그런데 그것도 첫 저장이 실패하면 뒤집힌 값을 메모리에 남긴 채 반환한다
  (`AssetAuthoringCommands.cpp:609-610`).
- **`tag.authoring.probe`는 이름과 효과가 가장 어긋난다.** "probe"인데 `add`/`remove`가
  프로젝트 자산을 **영구 편집**한다(쓰기가 프로세스 Finalize에 안착한다, 주석 :516).

**`mem.*` 별칭이 뭉친 이유가 응집이 아니었다.** 주석(`DiagnosticsCommands.cpp:862`)이
**MSVC의 C1061 중첩 한계** 때문이라고 적어 두었다 — 컴파일러 우회다. 그 아래
`mem.hook top`은 147줄짜리 하위 도구가 별칭 뒤에 숨어 있다.

**`commands.selftest`는 이 보고서의 어떤 중복도 구조적으로 못 잡는다.** 그것이 보는
것은 **이름** 충돌과 요약 누락이다. 위의 짝들은 전부 *다른 이름 · 다른 핸들러 함수 ·
같은 몸통*이라 보이지 않는다.

### 3.5 별칭 칸이 dispatch로 쓰이고 있다

이름 226 · 명령 212의 차이 14개 중 상당수가 **동의어가 아니라 서로 다른 동사**다.

```cpp
reg.Legacy({ "play", "stop" }, &Cmd_play);
reg.Legacy({ "ui.anchor", "ui.size", "ui.pos", "ui.screenpos" }, &Cmd_ui_anchor);
if (ctx.cmd == "undo") ...->Undo(); else ...->Redo();
if (cmd == "mem.reset") ... / if (cmd == "gc.delta") ... / if (cmd == "bt.reset") ...
```

`play`/`stop`, `undo`/`redo`는 반대 조작인데 descriptor 하나를 공유한다.
`cost`·`roles`·결과 계약이 하나로 묶이고 `commands.list`와 help는 같은 명령으로
보여 준다. **상태를 바꾸는 조작이 조회 descriptor 뒤에 숨으면**(`mem.reset`)
mutating 명령 전수를 registry로 셀 수 없다.

처분은 사례마다 다르다.

| 이름 | 소비자 | 처분 |
|---|---:|---|
| `mem.delta` · `mem.reset` · `mem.hook` · `dump.show` | **0** | ~~죽은 이름~~ → **틀렸다. 아래 ★** |
| `ui.size` · `ui.pos` · `ui.screenpos` | golden + 게이트 | `ui.anchor`의 인자 |
| `play`/`stop`, `undo`/`redo` | 다수 | 진짜 다른 동사 — descriptor를 가른다(이름은 이미 있으므로 명령이 늘지 않는다) |
| `scene.switch` (`scene.load` 별칭) | **34** | 진짜 동의어. 손대지 않는다 |

★ **"죽은 이름 4개" 는 2026-09-06 에 철회한다 — 넷 다 살아 있는 조작이다.**

소비자 수 0 을 "기능 없음" 으로 읽은 것이 오독이었다. 핸들러를 열어 보면 넷 다
자기 분기를 갖고 있다:

| 이름 | 실제로 하는 일 |
|---|---|
| `mem.hook` | CRT 할당 훅 — `on\|stack\|off\|top\|status` 모드를 가진 147줄 하위 도구(`_DEBUG` 전용) |
| `mem.reset` | 계수 기준선 재설정 |
| `mem.delta` | 기준선 대비 증감 |
| `dump.show` | 가장 최근 덤프의 **내용**까지 찍는다(`dump.list` 는 목록만) |

지우면 중복이 사라지는 것이 아니라 **진단 능력이 사라진다.** §3.4 의
`*.authoring.probe` 여섯과 같은 부류다 — 유일한 창구인데 소비자가 없을 뿐이다.

남은 진짜 문제는 §3.5 가 적은 그대로다: 이것들이 `aliases` 칸에 들어 있어
**descriptor 가 거짓말을 한다.** `commands.list`·help·`GET /commands` 는 넷을
`mem.stats`/`dump.list` 와 같은 명령으로 보여 주고, `cost`·`class`·요약이 하나로
묶인다. `mem.reset` 은 상태를 바꾸는데 조회 descriptor 뒤에 있다.

처분은 둘 중 하나이고 **이 문서가 고르지 않는다**:

- **(가) descriptor 를 가른다.** 이름은 이미 있으므로 명령이 느는 것이 아니라
  registry 가 정직해진다. 명령 수는 204 → 208 로 오르지만 그것은 지금까지
  **덜 세고 있었다**는 뜻이다.
- **(나) 모드 인자로 접는다.** `mem.stats hook top` · `dump.list show`. 이 저장소의
  주된 관례가 그것이고(`sparseresolver 0|1|print|probe|bench`,
  `experiment.catalog mount|probe`) 이름이 218 → 214 로 준다. 다만 `mem.*` 가
  뭉쳐 있는 이유는 응집이 아니라 **MSVC C1061 우회**였으므로, 그 관례를 따를
  근거가 이 자리에는 없다.

## 4. 축 B — undo 배선

### 4.1 212개 중 **1개**만 기록한다

저장소 전체에서 `UndoManager::Execute`를 부르는 CLI 핸들러는 하나다 —
`SceneObjectCommands.cpp:1723`, `object.create.undoable`. GUI 쪽 호출은 13곳이다.

코드에 그 결정이 적혀 있다:

> `// 기존 object.create는 Undo 스택을 건드리지 않는다. 그 성질에 이미 여러 게이트가`
> `// 기대고 있으므로 바꾸지 않고, 이력을 쌓는 별도 명령을 둔다.`

즉 `object.create.undoable`은 **`object.create`를 못 고쳐서 만든 우회로**다.
소비자 셋이 전부 undo 게이트 자신이라 제거에 딸린 파급이 없다.

### 4.2 비용이 생각보다 낮다 — `CustomChangeCommand`

6번째 undoable 타입 `Meta::CustomChangeCommand`
(`MakeCustomChangeCommand(undoFn, redoFn)`)가 있고, **GUI가 실제로 가장 많이 쓰는
것이 이것이다.** 새 `IUndoableCommand` 서브클래스 없이 람다 쌍으로 되는 것이
대부분이다.

단, 람다는 **엔티티를 `Entity::Index`로 잡아야 한다.** GUI의 기존 람다들은 raw
포인터를 캡처하고 있어 씬 재로드 뒤 dangling이 된다.

### 4.3 갈래

**GUI는 되는데 CLI만 안 되는 것 (진짜 비대칭)**

엔티티 생성 · 복제 · 삭제(**CLI 명령 자체가 없다**) · 모델 배치 · 선택 ·
반사 프로퍼티/트랜스폼 쓰기.

가장 싼 수정은 `model.place` — GUI가 이미 `LoadModelToSceneObjCommand`로 남기고
CLI만 안 남긴다. 인자 모양이 그대로 맞아 **3줄**이다.

**양쪽 다 없는 것 (비대칭이 아니라 미구현)**

rename · reparent · component add/remove · prefab create/instantiate.

**반대 방향 비대칭 — 이것이 함정이다**

`PrefabUtility::RecordPropertyOverride` 호출자가 **CLI 둘뿐이고 GUI는 0**이다.
GUI 인스펙터로 프리팹 인스턴스를 편집하면 override가 기록되지 않아 다음
`prefab.update`에서 조용히 날아간다. **"CLI를 GUI처럼 고치면 된다"가 아니다** —
그렇게 하면 이쪽이 퇴행한다.

**원리적으로 undo 불가**

디스크 쓰기(`scene.save` · `prefab.create/update` · `model.load` · 각종 authoring
probe) · 씬 전체 교체(`scene.load` · `scene.new` — 스택에 든 포인터가 전부 무효) ·
play 전이(그것이 스택을 비우는 사건) · `assets.unload`(자원 해제) ·
`prefab.update`의 인스턴스 fan-out(되돌릴 상태가 어디에도 없다).

### 4.4 게이트 구멍 둘

`verify-cli-editor-operation.ps1`은 에디터를 띄워 `undo.state`의 `editUndo` 증감을
**실측**한다 — 좋은 구조다. 배선하면 즉시 검증되고, 실패 문구에 "Undo를 늘린
것이라면 좋은 변경이니 래칫을 옮기라"고 이미 적혀 있다. 다만:

1. **14개 중 8개만 잰다.** 자기 머리말은 "14개 중 하나만"이라 적었는데 실제로 재는
   것은 8개다. `object.rootref`·`play`/`stop`·`prefab.*`·`undo`/`redo`가 빠져 있다.
   `engine_service`로 분류된 mutating 명령(`model.place`·`scene.select`·
   `render.matmode`·`ui.*`·`script.add`·`animator.*`)은 아예 밖이다.
2. **`object.transform` 케이스가 이름이 말하는 것을 검사하지 않는다.**
   `["OpProbeB","position","1","0","0"]`을 보내는데 핸들러는 `<이름> <px> <py> <pz>`를
   기대해 `"position"`이 `atof`로 **0.0**이 된다.

래칫에서 빠진 여섯은 `object.rootref` · `play` · `prefab.create` ·
`prefab.instantiate` · `prefab.update` · `undo`이고, **그중 넷이 mutator**다.

### 4.5 곁다리 — 치우지 않는 명령들

undo와 별개로, **활성 씬에 엔티티를 남기고 끝나는 명령이 넷** 있다:
`ui.navprobe`(엔티티 7 + 프리팹 2) · `perf.reflect`(N) · `serialize.bench prefab`(N) ·
`lifecycle.stress churn`(N). §3.4의 `*.authoring.probe` 다섯이 디스크에 남기는 것과
같은 성격이다 — 저작 중인 프로젝트에서 부르면 흔적이 쌓인다.

그리고 **조회처럼 생겼는데 쓰는 것이 둘** 있다. `gc.delta`와 `mem.stats`/`mem.delta`가
기준선이 없을 때 static 기준선을 설정한다(둘 다 의도이고 주석이 있다). §3.5의
"상태를 바꾸는 조작이 조회 descriptor 뒤에 숨는다"의 실제 사례다.

## 5. 축 C — `cost` 152개

**`cost`는 `Long`인지만 본다.** 거동을 읽는 곳이 둘뿐이고 둘 다 `Long` 비교다 —
`ConsoleCommandSystem.cpp:706`(긴 명령은 그 프레임을 혼자 쓴다)과
`EditorCommandServiceHost.cpp:167`(서비스가 동기로 답할지 202를 낼지). 나머지 다섯
참조는 전부 **공표**다(`commands.list`·`commands.describe`·`GET /commands`·구조화
결과). 따라서 **`Immediate`와 `Frames`는 오늘 런타임 동작이 완전히 같다.**

그래서 방향에 따라 성격이 다르다. `Frames`↔`Immediate`가 틀린 것은 **계약의
거짓말**이고, `Frames`인데 `Long`인 것은 **결함**이다 — 서비스가 초 단위 명령을
동기로 답하고 드레인을 독점한다. 서비스 기본 `mode`가 `"auto"`
(`CommandService.cpp:513`)라 명시하지 않는 소비자에게는 이 값이 실제로 작동한다.

> ★★★ **아래 §5.1 의 추정은 2026-09-06 실측이 뒤집었다. §6.6 을 먼저 볼 것.**
>
> "렌더 프로브 52개가 디바이스를 세우니 초 단위일 것" 은 구조로는 맞고 **크기로는
> 틀렸다.** Release 실측에서 62개 중 1초를 넘는 것은 **7개**뿐이고 절반 가까이가
> 100ms 미만이다. 디바이스를 세우는 것은 사실이지만 그 비용이 초 단위가 아니었다.
> 결과적으로 `cost` 를 고친 것은 55개가 아니라 **13개**다.

### 5.1 싸게 적힌 것 — 55개 (추정 · 뒤집혔다)

**렌더 프로브 52개는 한 핸들러 호출 안에서 그래픽 디바이스를 새로 세운다.**
추론이 아니라 구조다. `VulkanGridTest.cpp:107` `LoadLoader` → `:115`
`resources.Initialize`(인스턴스·물리/논리 디바이스·큐·검증 레이어) → `:121`
파이프라인 캐시 → `:153` 초기화, 그 로그 줄이 **"셰이더 SPIR-V 컴파일·파이프라인
생성 통과"**다. DX12 쪽도 26개 TU가 각자 `DX12DeviceResources resources;
resources.Initialize(...)`를 선언한다. `dx12.psocache`의 요약 "2회차 컴파일 0건"이
1회차에 PSO를 컴파일한다는 직접 증거다.

**같은 디렉터리의 형제 둘은 이미 `Long`이다** — `dx12.bench11`·`dx12.encoderbench`가
같은 `DX12DeviceResources` 패턴을 쓴다. 52개와 그 둘을 가른 것은 동작이 아니라
**요약에 "실측/bench"라는 낱말이 있느냐**였다.

측정된 것이 하나 있다:

| 명령 | 현재 | 근거 | 권고 |
|---|---|---|---|
| `script.reload` | frames | **실측 388–421 ms**(`EditorAutomationCLIPlan.md:1596-1600`, x64-Debug 3회). 프레임 p50 2.658 ms의 **약 145배**, 드레인 예산 2 ms의 약 200배 | `Long` |
| `shadermeta.probe` | frames | `DiagnosticsCommands.cpp:440-449` — 자산 루트 전체를 `recursive_directory_iterator`로 훑어 `.shadermeta`를 모아 전부 파싱. 같은 짓을 하는 `assets.modelbench`는 **이미 `Long`** | `Long` |
| `rhi.uploadsegments` | frames | 디바이스를 세운다. `liveness`도 `requires_restart` | `Long` |
| `scene.executiongraph bench` · `scene.sparseresolver bench` | frames | 호출자가 반복 횟수를 준다. 같은 모양의 형제 `proxybench`·`traversalbench`는 `Long`. `cost`는 명령당 스칼라라 **비싼 분기가 값을 정한다** | `Long` |

**지속 시간의 크기는 미확정이다.** 다만 재는 방법이 이미 있다 —
`Tools/regression/lc6-live-classification.ps1`이 LC6 때 렌더 명령 64개를 HTTP로
돌리며 **명령별 경과 ms를 TSV로 남겼다**(`:88-95`). 그 산출물이
`$env:TEMP\lc6live`로 가고 **커밋되지 않아** 계획서에는 판정 수(64 중 61 live)만
남았다. **그 스크립트를 다시 돌려 TSV를 남기면 한 번에 정해진다.**

간접 증거로는 소비자의 기대치가 있다 — 그 스크립트는 명령마다 **60초**를 주고
(`:11`, `timeoutMs: 60000`), `Invoke-Dx12Suite.ps1:62`는 프로세스마다 **300초**를 준다.

### 5.2 비싸게 적힌 것 — 순수 조회 11개

`dx12.live` · `render.backend` · `render.exposure` · `render.livecheck` ·
`render.rtinfo` · `render.shadowinfo` · `gpu.baseline` · `gpu.census`/`gpu.delta` ·
`ui.rect` · `ui.hitbox` · `script.fields` · `mem.stats`.

전부 핸들러 몸통을 읽어 확인했다 — GPU도, 파일 시스템도, 두 번째 프레임도 건드리지
않는다. `render.livecheck`는 요약이 "resize·다중 뷰 회귀 판정"인데 **아무것도
resize하지 않는다**(뮤텍스 둘 잡고 스냅샷 복사 후 포맷).

이것이 드러내는 내부 불일치: `pipeline.nodes`·`scene.dump`·`play.state`·
`undo.state`·`prefab.status`·`assets.modeldiag`는 **같은 성격인데 `Immediate`**다.

### 5.3 기본값이 아니라 블록 복사였다

`DescriptorSeed`는 `cost`에 **기본값을 주지 않는다.** 헤더 주석이 그것을 의도로
못 박았다(`CommandDescriptorSeeds.h:19-25` "★ 기본값을 **주지 않는다.**"). 212행이
전부 값을 명시한다 — 컴파일러가 강제한다.

그런데 배열이 이름순 정렬이라 **알파벳 이웃이 곧 작성 이웃**이고, 긴 구간이 동작과
무관하게 한 값을 공유한다:

| 구간 | 행 | 값 |
|---|---|---|
| `dx12.*` 33행 연속 | :73-107 | 전부 `Frames`. `dx12.ssgi`(디바이스+PSO+GPU 대기 4회)부터 `dx12.live`(캐시된 문자열 `printf`)까지 |
| `vk.*` 20행 연속 | :255-274 | **예외 없이** `Frames`. 스왑체인까지 세우고 PNG를 쓰는 `vk.selftest` 포함 |
| `render.*` | :198-210 | 전부 `Frames`. 순수 조회 셋과 `printf` 한 줄짜리 스텁 포함 |
| `experiment.*` | :108-131 | `Frames`가 기본, 이름에 "cook"이 든 것만 `Long` |

대조군이 있다. `cls`는 행마다 근거가 적혀 있고 정정 이력이 셋 남아 있으며
(`EditorAutomationCLIPlan.md:817-826`), `liveness`는 **실제 HTTP 실험으로 정했고 그
실험이 최초 가정을 뒤집었다**(:838-845). `cost`에는 그런 출처 기록이 **어디에도
없다.** 행별 근거가 달린 유일한 자리는 `script.invoke`이고, 그것도 측정이 아니라
"사용자 코드라 얼마 걸릴지 모른다"는 이유다.

즉 212번 적힌 것은 컴파일러가 시켜서고, 실제 패턴은 **접두 블록 일괄 지정 +
요약 문구에 bench/cook/corpus/build/import가 있으면 `Long`**이다.

### 5.4 고쳐도 깨지는 소비자가 없다

§5.1을 적용하면 55개가 `Long`으로 옮겨 분포가 `frames 152 / immediate 35 / long 25`
에서 대략 **`frames 86 / immediate 46 / long 80`**이 된다.

**새로 202를 받는 HTTP 소비자는 0이다.** `/command` POST 소비자는 저장소 전체에서
PowerShell 게이트 일곱뿐이고, 렌더 프로브를 HTTP로 부르는 유일한 소비자
(`lc6-live-classification.ps1:82`)를 포함해 **전부 `mode:'sync'`를 명시**한다.
기본 `auto`로 부르는 자리는 `verify-cli-service.ps1`의 `help`·`wait`·
`commands.describe`·`cli.echo.args`·`quit`뿐이고 전부 `Immediate`다.
`verify-cli-drain.ps1`은 오히려 `game.pak`(`Long`)이 **202로 오는 것을 단정**한다.

주 소비자인 `Invoke-Dx12Suite.ps1`은 **서비스를 아예 안 쓴다** — 테스트마다
`--script`로 프로세스를 띄운다. 터미널 경로에서 `cost`는 드레인 루프에만 영향을
주는데, 거기서는 어차피 그렇게 돌고 있다.

**실제로 붉어지는 것은 골든 하나다.** `cli_registry.golden.tsv`가 212행의 3열을
고정하므로 재분류한 행이 전부 `changed`로 뜬다. `-Update`로 다시 뜨면 된다 —
그것이 게이트가 설계대로 작동하는 모습이다.

선례도 있다. `scene.load`가 `Long`이 됐을 때 동기 단정 하나가 깨졌고, 그 처방이
`verify-cli-service.ps1:222-230`에 적혀 있다 — **기대값을 낮추지 말고 호출에
`mode:"sync"`를 명시하라.** 기대를 옮기면 검사가 조용히 사라지기 때문이다.

### 5.5 이 감사 밖이지만 같은 코드 경로에 있는 것

`dx12.selftest`·`vk.selftest`는 `requires_restart`, `crash.test`·`dump.crash`·`quit`는
`terminates_process`다. 이것들이 async/202 경로로 가면 **완료가 영영 오지 않는
operation 레코드**가 생긴다(`CommandService.cpp:566-575`가 enqueue 전에 running으로
표시한다). §3.4의 `dump.crash` 결함과 같은 자리다.

## 6. 실행 후보

*(순서만 제안한다. 무엇을 실행할지는 이 문서가 정하지 않는다.)*

| # | 일 | 성격 | 되돌리기 |
|---|---|---|---|
| ~~1~~ | ~~`dump.crash` 제거~~ | **완료 2026-09-05** | |
| ~~2~~ | ~~`render.exposure` 제거~~ | **완료 2026-09-05** | |
| 3 | `experiment.*` 계층 1+2 12개를 contract probe로 이관 | **5/12 완료 2026-09-05** (§6.2) | 나머지는 결정 필요 |
| 4 | `model.place` undo 배선 (3줄) | **배선 완료 2026-09-05 · 런타임 미검증** (§6.3) | |
| ~~5~~ | ~~`object.create` undo 배선 → `object.create.undoable` 제거~~ | **완료 2026-09-05** (§6.4) | |
| ~~6~~ | ~~`*scale` 셋의 요약·`cost` 정정~~ | **완료 2026-09-06** (§6.6) | |
| ~~7~~ | ~~죽은 별칭 이름 4개 제거~~ | **철회 2026-09-06** — 넷 다 살아 있는 조작이다(§3.5 ★). 처분은 (가)/(나) 결정 사항 | |
| 8 | `uploadring`/`uploadsegments` 짝 결정 | registry −1 | 결정 필요 |
| ~~9~~ | ~~`window.info`→`render.rtinfo`, `model.loadcached` 처분~~ | **철회 2026-09-06** — 양쪽 다 살아 있다(§6.7) | |
| ~~10~~ | ~~scene X0·X8 병합, 토글 둘~~ | **부분 완료 2026-09-06** — 토글 둘만 합쳤다(§6.7). X0·X8 은 기각 | |
| ~~11~~ | ~~게이트 확장~~ | **완료 2026-09-06** (§6.7) | |
| ~~12~~ | ~~`lc6-live-classification.ps1` 재실행 + TSV 커밋~~ | **완료 2026-09-06** (§6.6) | |
| ~~13~~ | ~~12번 수치로 `cost` 재분류~~ | **완료 2026-09-06 — 55개가 아니라 13개였다** (§6.6) | |

12번은 따로 떼어 둘 만하다. `cost`를 구조 추론으로 55개나 옮기는 것보다, **이미
있는 스크립트를 한 번 돌려 명령별 ms를 남기는 편이 싸고 정직하다.** LC6이 그
측정을 이미 했는데 산출물을 `$env:TEMP`로 보내 잃었을 뿐이다. `liveness`가
실험으로 정해져 최초 가정을 뒤집은 전례가 바로 옆에 있다.

1·2번이 가장 싸다 — 하나는 결함이고 하나는 죽은 코드다. 3번이 가장 크고(3,840줄)
그럼에도 안전하다: 오늘 아무 자동화도 그 12개를 돌리지 않는다.

**제거 시 절차**(모든 항목 공통): seed 행 · 핸들러 · `reg.*` 등록 줄 · 골든 행
넷을 함께 지우고 `verify-cli-registry-golden.ps1 -Update`로 골든을 다시 뜬다.
seed 행을 남기면 `commands.selftest`가 고아 descriptor로 붉어진다.

### 6.1 1·2번 실행 기록 (2026-09-05)

명령 **212 → 210**, 이름 226 → 224. `terminates_process` 3 → 2, `live` 206 → 205.

**`dump.crash` 의 결함을 실측으로 확인한 뒤 지웠다.**

| | 죽나 | `quit` 실행 | `Finalize` | exit |
|---|---|---|---|---:|
| `dump.crash throw` | **아니오** — `internal_error (command.exception)` | 예 | 완료 | 5 |
| `crash.test throw` | **예** (스택 19프레임) | 아니오 | 도달 안 함 | 3 |

`dump.crash` 는 자기 descriptor 에 `terminates_process` 라고 적고도 죽지 않았고,
따라서 덤프도 남기지 않았다. 일부러 죽어서 덤프 경로를 검증하는 것이 존재
이유이므로, 그 명령은 **자기 목적을 한 번도 달성한 적이 없다.**

남긴 쪽이 제 일을 하는지도 확인했다 — `verify-crash-dump.ps1` 이 `av`·`abort`·
`throw` 세 경로에서 각각 실제 덤프(스택 18·20·20 프레임)를 받고 통과한다.

**게이트**: registry-golden · discovery · exit-spine · exit-contract ·
consumer-contract · crash-dump 전부 통과.

★ 게이트를 돌리다 **가짜 초록을 하나 만들 뻔했다.** 여섯을 한 루프에서 `-Exe` 로
  돌렸는데 `verify-cli-consumer-contract.ps1` 은 그 인자를 받지 않는 정적 게이트라
  호출이 실패했고, `$LASTEXITCODE` 가 **앞 게이트의 0** 을 그대로 들고 있어 통과로
  보였다. 인자 없이 다시 돌려 진짜 통과를 확인했다.

### 6.2 3번 실행 기록 — 5/12 (2026-09-05)

명령 **210 → 205**, 이름 224 → 219. 옮긴 것: `weld` · `resolver` · `sampler` ·
`tangent` · `normal`.

새 자산 둘: `Tools/regression/experiment_contract_probe.cpp` (실행 파일 하나가
전부를 돌리고, 인자를 주면 그것만 돌린다) 와 `verify-experiment-contract.ps1`.
**검사 하나에 게이트 하나를 두지 않았다** — 명령 다섯을 게이트 다섯으로 바꾸면
줄이려던 그 실수를 이름만 바꿔 되풀이하는 것이다.

Debug/Release 양쪽에서 5건 전부 통과하고, **에디터를 띄우지 않는다.**

세 가지가 빌드에서 걸렸고 전부 게이트 주석에 남겼다:

1. **경고 수준을 소스 출처별로 나눠야 했다.** `/W4 /WX` 를 통째로 걸면 엔진
   `.cpp`(`CookedAssetManifest.cpp`)의 기존 경고로 이 게이트가 붉어진다. probe·
   self-test 는 `/W4 /WX`, 엔진 소스는 `/W0`, 엔진 헤더는 `/external:W0`.
2. **`/Fd:` 가 없으면 Debug 가 죽는다.** `/Zi` 가 PDB 를 작업 디렉터리(저장소
   루트)에 만들려다 실패하고, 컴파일 전체가 exit 2 로 끝난다.
3. **`/Fo:"...\"` 는 뒤의 소스 목록을 통째로 먹는다.** 끝 백슬래시가 닫는 따옴표를
   이스케이프한다. cl 은 `D8003: 소스 파일 이름이 없습니다` 만 내고 원인이 인용
   부호라는 말은 하지 않는다. `\\"` 로 써야 한다.

★ **대소문자 충돌을 하나 발견했다(고치지 않았다).** `VertexCacheOptimization.cpp`
는 vcpkg 의 `meshoptimizer.h` 를 노리고 `"meshoptimizer.h"` 를 include 하는데,
Windows 는 대소문자를 구분하지 않아 엔진 자신의
`Engine/RenderEngine/MeshOptimizer.h` 로 풀린다. 오늘 무사한 것은 RenderEngine 과
RenderTests 두 프로젝트가 include 순서를 **서로 다르게** 두기 때문이다. 한 번의
`cl` 호출로는 두 순서를 동시에 만족할 수 없어, `cacheopt` 이관이 이것을 다시
만난다. 지금은 그 TU 를 쓰지 않으므로 게이트에 주석으로만 적어 두었다.

파일은 `Editor/RenderTests/ExperimentParity/` 에 **그대로 뒀다.** 에디터 빌드
(`RenderTests.vcxproj`)에서만 뺐다 — 파일 이동과 명령 제거를 같은 커밋에 넣지
않는다(§12.3). 위치 정리는 순수 기계적 후속이다.

### 6.3 4번 실행 기록 — 배선 완료, 런타임 미검증 (2026-09-05)

`Cmd_model_place` 가 씬을 직접 `Instantiate` 하던 세 줄을, GUI 가 이미 쓰던
`Meta::LoadModelToSceneObjCommand` 를 `UndoManager::Execute` 로 태우는 것으로
바꿨다. **로직을 옮긴 것이 아니다** — 그 command 의 `Redo()` 가 여기 있던 것과
같은 일을 하고(옵션 조회까지 같다), `Undo()` 는 루트를 **인덱스로** 지운다.

▲ **이 기계에서 돌려 본 것은 아니다.** `model.place Prim_Cone` 이 성공 경로에
  닿지 못한다 — `assets.modeldiag` 가 `generationFromCatalog=0
  generationFromLibrary=0` 을 낸다. 게시된 model generation 이 하나도 없다.
  변경 **전에도** 이 명령은 같은 자리에서 빠져나왔으므로 회귀는 아니지만,
  "고쳤다" 와 "고친 것을 봤다" 는 다른 문장이다. LC8 의 Player smoke 를 막은 것과
  같은 환경 결손이다(`Dynamic_CPP/Assets` 대부분이 gitignore).

**대신 게이트를 넓혔다.** `verify-cli-editor-operation.ps1` 이 `editor_operation`
8개만 재고 있었고 — §4.4 에 적은 구멍 — `model.place` 는 class 가
`engine_service` 라는 이유로 밖에 있었다. GUI 는 Undo 를 남기고 CLI 만 안 남기던
바로 그 명령이 **분류 때문에 측정 대상이 아니었다.** 이제 잰다.

★ **측정 불가를 거짓값으로 적지 않는다.** 모델이 없으면 `editUndo` 는 당연히
  그대로인데, 그 0 을 "Undo 를 안 남긴다" 로 래칫에 기록하면 **고쳐 놓은 동작을
  안 고쳐진 것으로 못 박는다.** 측정 실패와 측정 결과는 다르다. 그래서 전제가 안
  서면 값을 만들지 않고 `exit 1` 로 낸다 — 조용히 건너뛰면 그 명령에 대해 이
  게이트는 영원히 초록이다.

  전제 판정은 **씬 오브젝트 수 변화**로 한다. 결과 message 로는 알 수 없다 —
  이 핸들러는 legacy 라 낼 message 가 없고 "찾을 수 없음" 은 stdout 으로만
  나간다(그 방식으로 먼저 짰다가 못 잡았다). 수가 늘었는데 `editUndo` 가
  그대로면 그것은 **진짜 판정**이고, 수도 안 늘었으면 측정 불가다.

그래서 이 게이트는 지금 이 기계에서 **붉다.** 모델 코퍼스가 있는 기계에서, 또는
`-ModelStem` 으로 있는 모델을 지정하면 판정이 난다. 기존 8개는 래칫과 그대로
일치한다(`object.create.undoable` 만 기록, 나머지 7개는 안 남김).

### 6.4 5번 실행 기록 (2026-09-05)

명령 **205 → 204**. `object.create` 가 GUI 와 같은 `Meta::CreateEntityCommand` 를
쓰게 하고, 그 우회로였던 `object.create.undoable` 을 지웠다.

이행 전후를 같은 시나리오로 각각 태웠다(이행 전 = 9/4 Release):

| | 이행 전 | 이행 후 |
|---|---|---|
| create 후 `editUndo` | **0** | **1** |
| `undo` 후 오브젝트 | **남음** | **사라짐**(redo 스택 1) |
| digest 의 parent 표기 | `1\|OcAlpha\|0\|…` | **같음** |
| `hierarchycheck` 오브젝트 수 | 2 | 1 |

★ **조사가 "부모가 갈린다" 고 적은 것은 틀렸다.** `object.create` 는 3번째 인자를
  생략해 `parentIndex = -1`, `CreateEntityCommand` 의 기본값은 `0` 이라 갈리는
  것처럼 보였다. 그런데 `Scene::CreateEntity` 안에
  `if (parentIndex >= m_Entities.size()) parentIndex = kSceneRootIndex;` 가 있고,
  `Entity::Index` 가 unsigned 라 `-1` 은 `INVALID_INDEX` 가 되어 **항상** 그 폴백에
  걸린다. 호출부만 읽고 판단한 결과였다. 위 표의 `|0|` 이 실측 확인이다.

`CreateEntityCommand` 에 `GetCreatedIndex()` 를 더했다 — 호출부가 생성 성공 여부를
알아야 하는데 command 가 그것을 삼키고 있었다. 형제인 `DuplicateGameObjectCommand`
가 같은 이름의 접근자를 이미 갖고 있다.

★★ **게이트에서 조용한 거짓 초록을 하나 만들 뻔했다.** 지운 케이스가
  `OpProbeB` 를 만들어 주던 자리였고, 뒤의 다섯 케이스가 그 이름을 쓴다. 그대로
  두면 그것들이 없는 오브젝트를 가리켜 아무것도 못 하고 `false` 를 기록하는데,
  **래칫에도 `false` 라 통과한다** — 판정이 맞아서가 아니라 양쪽이 다 아무것도
  안 해서 맞는 것이다. 픽스처 생성을 측정 밖으로 뺐다.

`verify-play-selection-undo.ps1` 도 이 명령을 쓰고 있었다(exit 2 로 붉어져서
알았다). `object.create` 로 옮겼고, 그 게이트의 무의미성 방지 검사가 push 가
실제로 먹었는지를 여전히 단정한다 — 통과.

**게이트**: registry-golden · discovery · exit-spine · exit-contract ·
consumer-contract · experiment-contract · play-selection-undo 통과.
editor-operation 은 §6.3 의 `model.place` 측정 불가로 붉다(의도).

### 6.5 곁가지로 나온 것 — 명령 서비스가 Release 에서 안 떴다 (2026-09-06)

§6 의 12번(`cost` 실측)을 하려고 Release 바이너리로 서비스를 띄우다 걸렸다.

```
[CLI] command service 시작 실패: socket 실패 (WSA 10093)
```

`WSA 10093` 은 `WSANOTINITIALISED` — `WSAStartup` 이 안 됐다는 뜻이다. 찾아보니
`SocketPlatform.h` 의 `SocketSubsystem` 이 **"프로세스당 한 번, Win32 의
`WSAStartup` 이 여기 산다"** 로 설계돼 있는데 **저장소 전체에서 인스턴스가 0**
이었다. 선언과 정의만 있고 아무도 만들지 않았다.

★ **그런데 Debug 에서는 떴다.** 다른 의존이 자기 목적으로 `WSAStartup` 을 해 주고
  있었고 서비스 소켓이 **남의 초기화에 얹혀** 있었다. Release 에는 그것이 없다.

★★ **왜 아무도 못 봤나 — 서비스 게이트 일곱이 전부 Debug 를 기본으로 쓴다.**
  `verify-cli-service` · `verify-cli-drain` · `verify-cli-editor-operation` ·
  `verify-cli-script-reload` · `verify-cli-script-invoke` ·
  `lc6-live-classification` 은 `-Exe` 기본값이 `x64-Debug` 이고,
  `verify-player-command-service` 는 `-Config` 기본값이 `Debug` 다.
  LC4~LC8 이 연 것 전부가 Debug 에서만 검증됐고, §18 의 "Editor Debug/Release 가
  통과한다" 는 서비스에 대해 **참인 적이 없었다.**

  Player 도 같은 `Service::Start` 를 쓰므로 이 수정이 함께 덮는다. 다만 이
  기계의 Release Player 는 9 월 2 일자 — LC8 이 `CommandService` 를 솔루션에
  넣기 전이라 서비스 자체가 없다. **Release Player 는 미검증으로 남는다.**

  이 저장소는 같은 모양을 이미 두 번 겪었다 — LC8 이 `build.ps1` 에 구성 표식을
  넣은 이유, scene 이행 때 9/4 Release 바이너리로 초록을 받을 뻔한 일. 셋 다
  "게이트가 초록이다" 와 "게이트가 **이 구성을** 보고 초록이다" 의 차이다.

고친 것은 `Service::Start` 에 함수 지역 static `SocketSubsystem` 하나다.
고친 뒤 `verify-cli-service.ps1` 을 **Release 로** 돌려 전체 통과를 확인했다.

### 6.6 12·13번 실행 기록 — `cost` 를 재서 고쳤다 (2026-09-06)

`lc6-live-classification.ps1` 을 **Release 로** 돌려 62개의 명령별 왕복 ms 를
얻었다. 산출물을 `Tools/regression/cli_cost.measured.tsv` 로 **커밋했다** —
LC6 때 이 측정을 이미 했는데 `$env:TEMP` 로 보내 잃었고(§5.1), `artifacts/` 는
gitignore 라 거기 두면 같은 일이 반복된다.

**분포 (Release, pass1)**

| 구간 | 수 |
|---|---:|
| ≥ 1,000ms | **7** |
| 100 ~ 999ms | 30 |
| < 100ms | 25 |

★ **가설이 뒤집혔다.** §5.1 은 "렌더 프로브 52개가 한 호출 안에서 그래픽
  디바이스를 세우니 `Long`(초 단위)" 이라고 적었다. 구조는 맞았다 — 실제로 세운다.
  **크기가 틀렸다.** 대부분 수십~수백 ms 다. include 표면으로 링크 폐포를 추정했다
  실측에 정정당한 §3.2 와 같은 종류의 오독이고, 이번에도 정정한 것은 측정이다.

**고친 13개**

| 방향 | 명령 | 실측 |
|---|---|---|
| `frames` → `long` | `dx12.forwardshade` 5,251 · `vk.gbuffer` 5,237 · `dx12.forwardscale` 1,930 · `dx12.scene` 1,656 · `dx12.forward` 1,491 · `vk.decal` 1,106 | ms |
| `long` → `frames` | `dx12.encoderbench` | **93ms** — 이름이 bench 라 비싸게 적혀 있었다 |
| `frames` → `immediate` | `render.livecheck` 6 · `render.matmode` 5 · `dx12.live` 4 · `render.backend` 4 · `render.shadowinfo` 4 · `render.rtinfo` 3 | ms |

`dx12.encoderbench` 가 반대 방향이라는 것이 §5.3 의 진단을 확인한다 — 값은 동작이
아니라 **요약 문구의 낱말**("bench")을 보고 정해지고 있었다.

분포: `long` 25 → **30**, `frames` 145 → **134**, `immediate` 34 → **40**.

**§6 의 6번도 같이 닫았다.** `*scale` 셋의 요약이 "해상도 스케일을 판정한다" 였는데
셋 다 **시간 비교 벤치**다. 문구를 실제 동작으로 고쳤다. `dx12.forwardscale` 만
`Long` 이고 `postscale`(950ms) · `ssaoscale`(603ms) 은 측정상 `Frames` 다 —
이름이 같은 계열이라고 값이 같지 않다.

★★ **`dx12.live` 는 pass1 4ms, 단독 재검 4,014ms 다.** 단독은 에디터를 새로
  띄우므로 첫 접촉 비용이 섞인다. 서비스는 **켜져 있는 에디터**에 붙는 창구이므로
  pass1(웜) 이 그 계약에 맞는 값이다. 두 열을 TSV 에 다 남겨 두었다.

**게이트**: registry-golden · discovery · service · drain · exit-spine 통과.
`drain` 은 `game.pak`(`Long`)이 **202 로 오는 것**을 단정하는 게이트라 이 변경의
직접 대조군이다.

### 6.7 9·10·11번 (2026-09-06)

**11번 — 게이트 확장. 완료.** `verify-cli-editor-operation.ps1` 이 7개를 재던 것을
**11개**로 넓혔다(`object.rootref` · `prefab.create` · `prefab.instantiate` ·
`prefab.update` 추가). 셋은 확인한 뒤에 넣었다 — 손으로 태워 보니 프리팹 파일이
실제로 써지고 `prefab.instantiate` 가 오브젝트를 2→3 으로 늘린다.

- `object.transform` 케이스의 인자를 고쳤다. `["OpProbeB","position","1","0","0"]`
  을 보내고 있었는데 문법은 `<이름> <px> <py> <pz>` 라 `"position"` 이 `atof` 로
  **0** 이 되어 (0,1,0) 을 넣고 있었다.
- **`play`/`stop` 과 `undo`/`redo` 는 일부러 뺐다.** 둘 다 편집 스택 **자체**를
  다루는 명령이라 "editUndo 가 늘었는가" 라는 질문이 안 맞는다 — `undo` 는 줄이는
  것이 일이고 `play` 는 진입할 때 비운다. 재면 늘 `false` 인데 그것은 결함이
  아니라 질문이 틀린 것이다. 그 둘은 `verify-play-selection-undo.ps1` 이 맡는다.
- 프리팹 케이스가 디스크에 남기는 파일을 `finally` 에서 지운다.
- **전체 무의미성 방지**를 넣었다: 실행 동안 씬 오브젝트가 늘지 않으면 실패다.
  §6.4 에서 픽스처가 사라져 케이스들이 헛돌 뻔한 일이 있었고, 그때 기록되는
  `false` 는 판정이 아니라 사고다. 실측 4 → 7.

**9번 — 철회. 양쪽 다 살아 있다.**

| 후보 | 실제 |
|---|---|
| `model.loadcached` | 소비자 **8곳**(verify-*.ps1 넷 포함). 그리고 `model.load` 와 **다르다** — `model.load` 는 `ImportSourceAsset` 로 **파일을 프로젝트에 복사**하고 `.meta` 를 쓴다. `loadcached` 는 그 쓰기 없이 로드만 한다. 지우면 읽기 전용 로드를 하려고 파일을 들여와야 한다 |
| `window.info` | `verify-resolution-sweep.ps1` 이 **해상도마다** 부르고, 그 값에서 기대 rect 를 계산한다. 합치려던 `render.rtinfo` 는 소비자가 사실상 없다 — **많이 쓰는 쪽을 지우고 안 쓰는 쪽을 남기는** 거래였다 |

§3.4 가 이 둘을 "중복" 으로 적은 근거는 **출력 값의 겹침**이었다. 값이 겹치는
것과 명령이 중복인 것은 다르고, 이번에도 그 구분을 소비자와 부작용이 갈랐다.
§7 의 첫 줄(소비자 텍스트 검색은 손 호출을 못 본다)과 같은 한계의 반대편이다.

**10번 — 부분 완료.** 근거가 선 것만 했다.

- ✅ **순수 토글 둘 → `scene.flag <이름> [0|1]`.** `scene.dirtytraversal`(33줄)과
  `scene.bonecache`(31줄)가 접근자 쌍과 문자열 넷을 빼면 **줄 단위로 같았다** —
  조작 하나에 64줄의 near-duplicate. 명령 2 → 1. 인자 없이 부르면 전부 조회하고,
  모르는 이름은 가능한 목록과 함께 거부한다.
  `traversalbench` 인자로 접지 **않았다**: 그 벤치는 이 플래그들을 읽어 헤더에
  찍는데(`dirtytraversal=%s bonecache=%s`), 벤치 전용 인자로 만들면 다른 명령을
  이 플래그 아래에서 재 볼 방법이 사라진다. 플래그는 프로세스 전역이다.
- ❌ **X8(`proxydirty`+`proxybench`) 기각.** 둘은 서로의 조합이 아니고 어느 쪽도
  중복이 아니다(단정 대 측정). 합칠 근거는 "다른 슬라이스가 `probe|bench` 한
  이름을 쓴다" 는 **스타일**뿐이라 §0 의 기준에 걸리지 않는다. 게다가
  `verify-render-proxy-dirty.ps1` 이 `[scene.proxydirty]`·`[scene.proxybench]`
  마커를 정규식으로 읽어, 합치면 마커가 **없는 명령 이름을 가리킨다.** registry
  한 행을 얻고 그 불일치를 사는 거래다.
- ❌ **X0(`transformstats`+`traversalbench`) 기각.**
  `docs/plans/TransformUpdatePlan.md` 가 그 슬라이스의 **진입점을 둘로 명시**한다.
  설계 의도를 스타일로 뒤집지 않는다.

명령 **204 → 203**, 이름 218 → 217.

**게이트**: registry-golden · discovery · exit-spine · render-proxy-dirty 통과.
editor-operation 은 `model.place` 측정 불가로 붉다(§6.3, 의도).

★ **이 세 항목에서 내 감사가 두 번 더 틀렸다.** 7번(별칭 넷)·9번(둘) 전부
  "소비자 수 0" 또는 "출력 겹침" 을 **중복**으로 읽은 것이었다. §1 의 한계 항목에
  하나를 더한다: **구조적 유사성은 중복의 증거가 아니다.** 부작용(파일을 쓰는가)과
  의존 방향(누가 누구를 부르는가)을 봐야 갈린다.

## 7. 이 조사가 **하지 않은** 것

- 명령을 하나도 지우거나 고치지 않았다. 코드 변경 0.
- 손으로 부르는 사용은 보지 못한다. 폐기 판단의 전제가 되지 못한다.
- `experiment` 계층 3·4(활성 계획 소속)는 처분을 정하지 않았다.
- `matinstance`·`matseal`은 `Texture.h`를 끌어와 standalone probe의 include 표면이
  `mathematics_contract_probe.cpp`보다 넓다. **빌드 스파이크가 먼저다.**
- `cost` 재분류의 근거는 **구조 추론이지 측정이 아니다**(`script.reload` 388–421 ms
  하나만 실측). §6의 12번을 먼저 하는 이유다.
- `commands.dump`↔`commands.list`는 둘 다 살아 있는 소비자가 있고 **두 스크립트가
  각자 자기가 정본이라고 적어 두었다**. 처분을 정하지 않았다.

## 8. 부록 — 실측 하나

`scene` 조사가 "probe 픽스처의 `Destroy()`가 지연이라 같은 배치에서 바로
`scene.save`하면 픽스처가 저장 문서에 실릴 수 있다"를 미검증 위험으로 올렸다.
코드만 보면 실재해 보인다 — `SaveScene`은 `FlushPendingDestroy`를 부르지 않고,
`Scene::reflect()`는 `meta::field<&Self::m_Entities>`로 벡터를 통째로 낸다.

태워 봤다(`scene.new` → `scene.proxydirty probe` → `scene.save`, Debug 바이너리):

```
[scene.proxydirty] dedupe=PASS publish=5 folded=4 drained=1 mask=0x1f
[scene.proxydirty] generation=PASS drained=2 stale=1 committed=1 probe=PASS
[CLI] 씬 저장: leak.creator (912 바이트)
저장된 엔티티 이름: leak, Transform
```

**누출되지 않았다.** probe는 실제로 픽스처를 만들었고(`publish=5`) 저장 문서에는
루트와 `Transform`만 있다. **이 시나리오에서 안 난다는 것만 확인했고, 어떤
경로로도 안 난다고 증명한 것은 아니다.**
