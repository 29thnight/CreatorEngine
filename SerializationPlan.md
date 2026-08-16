# 직렬화 이원화 — 저작 텍스트 · 런타임 쿠킹 바이너리 (PHASE 17)

2026-08-16. "yaml이 무겁고 느린 것 같다 — 기존 구조 호환을 신경 쓰지 않는다면
어떻게 리팩토링할 것인가"라는 물음에서 출발했다. 전수 조사의 결론은 **질문의
축을 바꿔야 한다**는 것이다: "YAML을 무엇으로 교체하나"(일차원)가 아니라
"저작 포맷과 런타임 포맷을 분리하나"(이차원)가 올바른 축이다. 목표 사슬:

```
에디터 저작(YAML 텍스트, git-diff 가능)
    → Cook(리플렉션 순회 → 바이너리 + GUID 매니페스트)
    → pak → Player(바이너리 로드만, yaml-cpp 호출 0)
```

전제: 사용자가 **기존 파일 호환 무시를 허용**했다. 이 전제는 §7의
SceneGraphRedesignPlan §5("파일 포맷 불변·일괄 변환 금지")와 충돌하므로
§7에서 순서로 조정한다.

---

## 1. 지금 무엇이 있는가 — 측정 (2026-08-16)

### 1.1 데이터 규모 — 절대량은 작고, 문제는 반복이다

| 항목 | 값 |
|---|---|
| 씬 `.creator` | 12개 · 239KB (최대 Test1 49KB) |
| 프리팹 `.prefab` | 206개 · 3.9MB (최대 UI_CanvasesIngame 462KB) |
| `.meta` | 598개 · 91KB (평균 156B) — **git 추적 28개뿐**, 나머지 로컬 자동 생성 |
| yaml-cpp 소비 파일 | 31개 (직접 `YAML::` + `MetaYml::` 별칭) |
| 직렬화 성능 실측 | **계획 문서 6종 어디에도 없음** — D0가 기준선을 만든다 |

yaml-cpp 파싱 처리량은 통상 10~20MB/s급(rapidjson의 1/20 이하)이지만, 총량
4.1MB에서 파서 교체만으로 얻을 절대 이득은 제한적이다. 진짜 비용은 아래
1.2~1.5의 구조에 있다.

### 1.2 런타임(Player)이 에디터와 같은 비용을 낸다

- pak은 **원본 텍스트를 무변환 운반**한다. `PackageGameAssets()`
  (`Utility_Framework/PakHelper.h:100-267`)는 Assets 트리 전체를 파일 단위로
  `addFile` — 쿠킹 없음. Player는 부팅 시 `%TEMP%\UnpackedAssets`에 언팩
  (`EngineSetting.cpp:61-69`) 후 **씬 전환마다** `MetaYml::LoadFile`
  (`SceneManager.cpp:410,618,711`)로 전체 텍스트를 재파싱한다.
- ★ **Player가 에디터 인프라를 그대로 구동한다.** `DataSystem::Initialize`가
  에디터/플레이어 동형이라(`PlayerMain.cpp:124`), 게임 실행 파일이 부팅 시
  전체 트리 `.meta` 스캔·생성(`ScanAndGenerateMissingMeta`, 가드 없음)을
  수행하고 **efsw 파일 워처를 프로세스 종료까지 띄운다**(`DataSystem.cpp:93-101`).
  `BUILD_FLAG`는 `ScanAndCleanupInvalidMeta`만 배제한다.
- ★ **pak 필터가 죽은 코드다.** `scriptExtensions{".h",".hpp",".cpp"}` 배열이
  선언만 되고 사용되지 않아(`PakHelper.h:164-168`), `.meta`뿐 아니라 **스크립트
  소스까지** pak에 실려 나간다.

### 1.3 리플렉션 YAML 계층 — 코어는 좁고, 새어나간 표면이 넓다

형식 변환 코어는 4개 헤더에 응집된다: `ReflectionYml.h`(Serialize/Deserialize/
DeserializePrefab/ExtractTypeFromYAML), `ReflectionYamlTemplete.h`(스칼라 특수화
23종 + 벡터 17종 테이블), `ReflectionVectorInvoker.h`/`ReflectionVectorMapper.h`.
백엔드 교체 자체는 좁다. 문제는 `YAML::Node`가 **값 타입으로 코어 객체에 상주**
하는 것이다:

| 상주 지점 | 용도 |
|---|---|
| `Prefab::m_prefabData` (`Prefab.h:35`) | 파싱된 트리를 프로세스 생애 동안 보관 |
| `GameObject::m_prefabOriginal` (`GameObject.h:177`) | 인스턴스화에 쓰인 서브트리 사본 — 패치 diff용 |
| `GameObjectCommand::m_serializedNode` (`GameObjectCommand.h:101`) | Undo/Redo 스냅샷 |
| `SceneManager` 시그니처 4곳 + `m_editorSceneBackup` | `DesirealizeGameObject` 계열 |
| `ComponentFactory::LoadComponent` | **수기 노드 접근 27곳** (680줄, 리플렉션 밖 특례) |

구조적 비용 둘: ① 상속 단계마다 부모 노드를 만들어 키 단위 재복사
(`ReflectionYml.h:71-79`) — 깊이 N이면 최상위 프로퍼티가 N번 복사된다.
② 프리팹 오버라이드 판정이 `YAML::Dump` **문자열 생성 후 비교**
(`ReflectionYml.h:348`)다. 이 둘은 포맷을 바꿔도 그대로 따라간다.

프리팹은 파싱 1회 후 트리 상주 구조라(캐시 `m_prefabCache`,
`PrefabUtility.cpp:181-201`) Instantiate마다 재파싱은 없지만, yaml-cpp Node는
내부가 shared_ptr 덩어리라 **트리 워크(맵 룩업 + `as<T>()`) 자체가 싸지 않다.**

### 1.4 포맷이 이미 3갈래다

| 트랙 | 대상 | 비고 |
|---|---|---|
| YAML | 씬·프리팹·`.meta`·EngineSettings·TagManager·물리 매트릭스·블랙보드·`.bt`·`.volume`·`.foliage`·머테리얼 `.asset` | 머테리얼은 `YAML::Binary`로 상수버퍼 바이너리 임베드(`DataSystem.cpp:449,491`), GameBuild에선 `LoadMaterial`이 컴파일에서 빠져 nullptr 반환 |
| JSON (nlohmann) | 애니메이터 상태그래프 `.json` · 입력맵 `.json` · 터레인 본문 | ★ **애니메이터는 같은 데이터가 씬 YAML(`Animator.h:91` `[[Property]]`)과 에디터 `.json`으로 이중 저장** · 터레인은 본문=JSON인데 사이드카 `.meta`=YAML(`Terrain.cpp:524-538`) |
| json 고정 계약 | `ISerializable.h` — `virtual nlohmann::json SerializeData()` | 인터페이스 수준에서 포맷이 박제됨 |

### 1.5 `.meta`는 sidecar의 비용만 내고 가치는 못 얻는다

- GUID는 UUIDv5 결정적 해시인데 **파일명만** 해시한다(`AssetMetaWather.h:330`
  `MakeFileGUID(targetFile.filename().string())`). 결과 둘: ① 다른 폴더의
  동명 파일이 같은 GUID — `AssetMetaRegistry::Register`는 충돌 검사 없이
  후입이 선입을 조용히 덮어쓴다(`AssetMetaRegistry.h:7-11`). ② **리네임하면
  GUID가 바뀐다** — sidecar의 존재 이유인 "이동/리네임에도 참조 불변"이
  성립하지 않는다.
- `.gitignore`가 `*.meta`를 무시한다(140, 465행). 결정적 해시라 로컬
  재생성으로 때우는 설계인데, 그 결정성이 ①②의 원인이다 — 서로가 서로를
  정당화하는 순환.
- 씬/프리팹은 이미 GUID로 애셋을 참조한다(`m_fileGuid` 등, ComponentFactory·
  BehaviorTree·Volume·Foliage·PrefabUtility 소비 확인). 단 프리팹 인스턴스
  재연결(`m_prefabFileGuid` 소비)은 주석 처리된 죽은 코드고
  (`SceneManager.cpp:1104-1112`), `LoadPrefab(name)`은 경로 기반 — GUID·경로
  참조가 혼재한다.
- `Uuid.h:14-31`은 boost 바이트 동일성을 골든 비교로 검증했다고 명시 —
  알고리즘 변경은 기존 `.meta`·씬 참조 전부를 무효화한다. **호환 무시 전제가
  이 족쇄를 푸는 유일한 기회다.**

---

## 2. 결함 목록

| # | 심각도 | 내용 | 위치 |
|---|---|---|---|
| Y-1 | HIGH | 동명 파일 GUID 충돌 + 레지스트리 무경고 덮어쓰기 — 참조가 엉뚱한 애셋으로 해석될 수 있다 | `AssetMetaWather.h:330` · `AssetMetaRegistry.h:7-11` |
| Y-2 | HIGH | 리네임 시 GUID 변경 — 참조 안정성 부재. sidecar 설계 목적 미달 | 같은 곳 |
| Y-3 | HIGH | Player가 `.meta` 생성 스캔 + efsw 워처를 런타임 내내 구동 | `DataSystem.cpp:93-101` · `PlayerMain.cpp:124` |
| Y-4 | MEDIUM | pak 필터 죽은 코드 — `.cpp/.h/.hpp` 소스와 `.meta`가 게임 배포물에 포함 | `PakHelper.h:164-168, 208-240` |
| Y-5 | MEDIUM | 애니메이터 데이터 이중 직렬화(씬 YAML + 에디터 json) — 두 사본의 어긋남은 시간 문제 | `Animator.h:91` · `Animator.cpp:308,333` |
| Y-6 | MEDIUM | 프리팹 오버라이드 diff가 `YAML::Dump` 문자열 비교 — 포맷 교체의 직접 장애물이자 비용 | `ReflectionYml.h:348` |
| Y-7 | LOW | `AnimationController::Deserialize()` 본체가 비어 있는 죽은 오버라이드 | `AnimationController.cpp:463` |
| Y-8 | LOW | 프리팹 인스턴스 재연결 로직 주석 처리 — 저장은 하지만 안 쓰는 필드(`m_prefabFileGuid` 소비부) | `SceneManager.cpp:1104-1112, 1164-1172` |

---

## 3. 설계 결정

### 3.1 저작 포맷 — YAML 유지

호환 무시 전제에서도 저작 포맷을 바꾸지 않는다. 근거 셋:

1. 저작물의 가치는 git diff/merge·충돌 해결·손상 시 수동 복구·텍스트 검색에
   있고, 저작 시점 성능은 병목이 아니다(1.1).
2. MaterialPipelinePlan §3.2가 `.shader` DSL을 **YAML로 통합**하기로 이미
   결정했고("파서를 하나 더 유지할 이유가 없다"), SceneGraphRedesignPlan §5의
   포맷 예외 1~5가 전부 YAML 위에 설계됐다. 저작 포맷 교체는 두 계획을 다시
   여는 비용 대비 얻는 것이 없다.
3. 성능 문제의 답은 저작 포맷 교체가 아니라 **런타임에서 텍스트 파서를
   치우는 것**(3.2)이다.

JSON 통일안은 기각 — 제2 트랙과 합치는 이득보다 1·2의 비용이 크다. 대신
JSON 트랙 자체를 정리한다(3.5).

### 3.2 런타임 포맷 — 리플렉션 기반 커스텀 바이너리 (쿠킹 산출물 = 캐시)

| 후보 | 판정 | 근거 |
|---|---|---|
| **커스텀 바이너리** (리플렉션 테이블 순회 직렬) | **채택** | 이 엔진은 `Meta::Type`이 직렬화의 단일 진실이다 — 쿠킹은 "리플렉션 순회 결과를 키 없이 순서대로 쓰기 + 문자열 테이블"로 충분하다. `ModelLoader`의 모델 바이너리 캐시(`ModelLoader.cpp:526,753`)가 사내 선례 |
| FlatBuffers | 기각 | 스키마 이중 관리 — 리플렉션 시스템이 있는데 스키마 언어를 또 세운다 |
| CBOR/MessagePack (nlohmann 내장) | 기각(과도기 대안으로만 기록) | 추가 의존성 0은 장점이나 자기서술 포맷이라 키 문자열이 반복되고 이득이 제한적 |

★ **버전 관리는 "재쿡"으로 대체한다.** 쿠킹 산출물은 캐시다 — 원본 YAML에서
언제든 재생성 가능해야 하고, 포맷 버전이 바뀌면 마이그레이션이 아니라 전체
재쿡한다. 이 원칙이 스키마 진화 기능(FlatBuffers의 존재 이유)을 불필요하게
만든다. 산출물에는 포맷 버전 + 리플렉션 레이아웃 해시를 박아 불일치 시
쿠킹 실패로 낸다(조용한 오독 금지).

### 3.3 Node 추상화 격리 — 포맷 전환의 이음새이자 독립 가치

`YAML::Node` 상주 3곳(1.3)과 `MetaYml::Node` 시그니처 노출을 자체 중간 표현
(최소 인터페이스: 맵/시퀀스/스칼라 접근 + 구조 비교) 뒤로 격리한다.
`DeserializePrefab`의 Dump 문자열 diff는 이때 **구조적 동등성 비교**로
교체된다(Y-6) — 텍스트 직렬화 결과에 의존하는 알고리즘은 포맷 이원화와
양립할 수 없다. 이 슬라이스는 포맷 전환을 하지 않더라도 결합도 관점에서
독립적으로 가치가 있다.

### 3.4 `.meta` — 존치하되 재정의: 랜덤 GUID + git 추적

- **폐지 + 중앙 DB안 기각**: 임포트 설정(모델 3종 옵션 · `.cpp`의
  reflectionFlag/이벤트)의 저장처가 필요하고, 중앙 단일 파일은 팀 머지
  충돌의 단일점이 된다.
- **채택**: GUID를 **랜덤 UUIDv4 채번**으로 바꾸고 `.meta`를 **git 추적
  대상**으로 전환한다. 이때 비로소 Y-1·Y-2가 동시에 해소된다 — 리네임/이동은
  `.meta`가 따라가므로 참조 불변, 동명 파일은 서로 다른 GUID. 결정성을 잃는
  대신 `.meta` 커밋이 진실이 된다(Unity와 같은 규약: "애셋을 옮길 때 .meta를
  같이 옮겨라").
- 기존 애셋은 호환 무시 전제 하에 **일괄 재채번 + 참조 재작성 스크립트
  1회**로 전환한다(씬 12 + 프리팹 206 + `.asset`류 — 참조 필드는 전부
  `m_fileGuid`·`m_prefabFileGuid`·`m_scriptGuid` 계열 문자열이라 기계 치환
  가능). 레지스트리에는 충돌 시 경고 로그를 넣는다(무경고 덮어쓰기 금지).
- **런타임에서 `.meta`는 퇴출한다**: 쿠킹이 GUID→경로(→pak 오프셋) 매핑을
  매니페스트로 굽는다(3.6). 그 전까지 Player는 `.meta`를 **등록 전용**으로만
  읽는다(생성·워처 없이).

### 3.5 JSON 트랙 정리

- **애니메이터(Y-5)**: 씬 YAML 리플렉션 경로를 단일 진실로 하고, 에디터
  `.json` 별도 저장을 은퇴시킨다(에디터 UI는 리플렉션 경로 위에서 동작).
  빈 `Deserialize()`(Y-7)도 함께 제거.
- **`ISerializable`**: json 고정 계약을 Node 추상화(3.3) 기반으로 개정 —
  구현체가 적으므로 표면은 작다(실측 후 D4에서 확정).
- **입력맵·터레인 본문**: 동작하는 독립 파일 포맷이므로 강제 통합하지
  않는다 — 단 터레인의 "본문 JSON + 사이드카 YAML" 혼재는 D2의 `.meta`
  재정의를 그대로 따르면 자연 해소된다(사이드카 포맷은 그대로, 의미만 재정의).

### 3.6 쿠킹과 pak 매니페스트

BuildPipelinePlan §2.3의 Cook 단계(현재 hlsl→cso만)에 씬·프리팹·`.asset`
쿠킹을 추가한다 — 같은 계획 §4가 "에셋 포맷 쿡"을 의도적으로 범위에서
제외하며 남겨둔 자리(AssetResidencyPlan 연계 예정)를 이 문서가 채운다.
pak에는 매니페스트(GUID → 가상 경로/오프셋)를 함께 굽고, Player의
`AssetMetaRegistry`는 매니페스트에서 직접 구축된다 — 부팅 스캔·efsw·pak 내
`.meta` 동봉이 전부 불필요해진다.

---

## 4. 이행 — 슬라이스

원칙은 다른 페이즈와 같다: **슬라이스마다 소비자가 먼저 있고, 판정은
수치다.** 공통 판정: 회귀 세트(Tools/regression, pwsh 필수) 통과 +
프리팹 왕복 검사(`verify-prefab-roundtrip.ps1`) 통과.

**D0 — 계측: 기준선 확정 (0.5일)**
직렬화 성능 실측이 0인 상태(1.1)에서 시작하지 않는다. 씬 전환 1회를
파싱(LoadFile)/역직렬화(Meta::Deserialize)/컴포넌트 특례(ComponentFactory)로
분해 계측하고, 최대 프리팹(UI_CanvasesIngame 462KB)의 로드·Instantiate를
따로 잰다. PHASE 14 프로파일러 스팬을 쓴다. 산출: 이 문서에 기준선 표 추가.
**모든 후속 슬라이스의 이득은 이 표 대비 수치로 판정한다.**

**D1 — 런타임 위생 (Y-3·Y-4, 1일)**
`ScanAndGenerateMissingMeta`를 "등록 전용 스캔"과 "생성"으로 분리하고 Player는
전자만 수행. efsw 워처는 에디터 전용으로 가드. pak 수집에서 `.cpp/.h/.hpp`
제외(죽은 `scriptExtensions`를 실제 필터로) — ★ `.meta`는 D5 매니페스트
전까지 Player의 GUID 해석에 필요하므로 **이 슬라이스에서 제외하지 않는다**.
판정: Player 프로세스에 efsw 스레드 0 · pak에 소스 파일 0 · 게임 정상 기동.

**D2 — GUID 정체성 수술 (Y-1·Y-2, 2일)**
랜덤 UUIDv4 채번 + `.meta` git 추적 전환(3.4) + 일괄 재채번·참조 재작성
스크립트 + 레지스트리 충돌 경고. 죽은 프리팹 재연결 코드(Y-8)는 살리거나
지우거나 이 슬라이스에서 결정한다(트랙 P 진행 상황에 따름 — §7).
판정: 동명 파일 2개가 서로 다른 GUID · 파일 리네임 후 씬 로드 시 참조 유지 ·
전체 씬 12종 로드 왕복 diff 0(재채번 직후 1회 재저장 기준).

**D3 — Node 추상화 격리 (Y-6, 3일)**
상주 3곳 + SceneManager 시그니처 + ComponentFactory 27곳을 중간 표현 뒤로
(3.3). Dump 문자열 diff → 구조 비교. 코어 4헤더는 이 시점엔 YAML 백엔드
그대로 — 동작 불변 리팩터다. 판정: `yaml-cpp/yaml.h` include가 코어 4헤더 +
어댑터 1곳으로 수렴(ScriptBinder·EngineGUIWindow에서 직접 include 0) +
회귀·왕복 검사 통과 + D0 대비 성능 비퇴행.

**D4 — JSON 트랙 정리 (Y-5·Y-7, 2일)**
3.5. 애니메이터 단일 진실화가 본체. 판정: 애니메이터 데이터의 저장 경로
1개 · `.json` 사이드 파일 미생성 · 애니메이터 회귀(상태 그래프 로드 후
전이 동작) 통과.

**D5 — 쿠킹 바이너리 + pak 매니페스트 (4일)**
리플렉션 순회 바이너리 라이터/리더(3.2) + Cook 단계 확장 + 매니페스트(3.6).
Player 로드 경로를 매니페스트+바이너리로 전환하고 `.meta`를 pak에서 제외.
★ 최대 위험 슬라이스 — "배선만 이으면 그림이 더 나빠진다"(Forward+ 전례)를
직렬화판으로 반복하지 않기 위해, **쿠킹 왕복 검사**(YAML 로드 결과 ==
바이너리 로드 결과, 리플렉션 프로퍼티 전수 비교)를 먼저 세우고 배선한다.
판정: 왕복 검사 전 씬·전 프리팹 통과 · Player 씬 전환 시간 D0 대비 수치
개선(목표치는 D0 결과를 보고 이 문서에 추가) · 게임 빌드 실플레이 확인.

**D6 — Player 텍스트 경로 은퇴 (1일)**
EngineSettings·TagManager 등 부팅 YAML도 쿠킹 대상에 편입. 판정:
**Player 실행 중 `YAML::LoadFile` 호출 0**(로그 훅 계측) — yaml-cpp 링크
자체의 제거는 Utility_Framework 계층 분리가 필요하므로 목표로 하지 않는다
(호출 0이 실질 기준).

**D7 — 에디터 웜 캐시 (선택, 1일)**
에디터도 쿠킹 캐시를 mtime 비교로 활용해 반복 로드를 가속. D5의 부산물이
공짜로 생기는지 보고 결정 — 안 되면 하지 않는다(YAGNI).

합계 **14.5일**(D7 제외 13.5일). 순서 제약: D0 → D1·D2 병행 가능 → D3 →
D4·D5 병행 가능(D5는 D2의 GUID 확정 선행 필수) → D6. D3가 전체의 이음새다.

---

## 5. 완료 기준

1. **Player 실행 중 `YAML::LoadFile` 호출 0** — 텍스트 파서는 에디터의 것.
2. **씬 전환 시간 D0 기준선 대비 수치 개선** — 목표치는 D0 후 확정.
3. **리네임/이동/동명 파일에서 참조 불변** — GUID 충돌 경고 0.
4. **pak에 소스 파일·`.meta` 미포함**, 매니페스트로 해석.
5. **저장 경로 수 감소**: 애니메이터 1경로 · 머테리얼 1경로(MaterialPipelinePlan
   M5의 "직렬화 단일화"와 같은 기준을 공유).
6. **쿠킹 왕복 검사 상설화** — 회귀 세트에 편입, 전 씬·전 프리팹 자동 비교.
7. 회귀 세트 · 프리팹 왕복 검사 착수 전과 동일 판정.

## 6. 하지 않을 것

- **저작 포맷 교체** — 3.1. YAML 텍스트가 저작의 왕좌를 유지한다.
- **FlatBuffers/스키마 언어 도입** — 3.2. 리플렉션이 스키마다.
- **쿠킹 산출물의 하위 호환** — 캐시다, 재쿡한다.
- **입력맵·터레인 본문 JSON의 강제 YAML화** — 동작하는 독립 포맷이다(3.5).
- **yaml-cpp 링크 제거를 완료 조건으로 삼기** — 호출 0이 기준, 링크 분리는
  EngineLayerSeparationPlan의 몫.
- **상속 재복사(1.3 ①) 최적화** — 쿠킹이 서면 런타임에서 사라지는 비용이다.
  에디터 저장 시점 비용은 D0 계측이 문제로 지목할 때만 별도 슬라이스로.

## 7. 다른 계획과의 관계

- **SceneGraphRedesignPlan §5 "파일 포맷 불변·일괄 변환 금지"와 정면
  충돌한다.** 그 조항의 목적은 트랙 G/E/K/S/P 진행 중 직렬화가 흔들리는 것을
  막는 것이므로 폐기가 아니라 **순서 조정**으로 푼다: D2(일괄 재채번)와
  D5(쿠킹)는 **E1·P2(왕복 검사 `-Strict` 승격) 이후**에만 착수한다.
  D0·D1·D3·D4는 파일 포맷을 건드리지 않으므로 즉시 가능하다.
- **BuildPipelinePlan**: §2.3 Cook 단계와 §4가 남겨둔 "에셋 포맷 쿡" 자리를
  D5가 채운다. pak 원본 운반("문제는 용량·로드 시간이지 정확성이 아니다")의
  성능 절반이 여기서 닫힌다. Paklib의 lz4 코덱 TODO(U2, 미결)는 별개 —
  쿠킹된 바이너리는 압축 없이도 이득이고, 압축은 그 위에 얹는 독립 결정이다.
- **MaterialPipelinePlan M5**: "직렬화 단일화 — DataSystem 경로 하나로"와
  D3(ComponentFactory 특례 수렴)가 같은 방향이다. M5가 먼저 오면 D3의
  머테리얼 몫이 줄고, 반대면 D3가 M5의 기반이 된다 — 먼저 도착하는 쪽이
  기준.
- **UtilityFrameworkModernizationPlan**: 리플렉션 계층(ReflectionYml 등)이
  Utility_Framework 소속이므로 D3의 파일 이동/개명이 생기면 그 문서의
  인벤토리를 갱신한다.

## 8. 리스크

- **재채번 스크립트의 참조 누락** — GUID가 문자열로 박힌 곳이 씬·프리팹
  밖에도 있다(모델 바이너리 캐시의 머테리얼 GUID `ModelLoader.cpp:526,753`).
  D2에서 GUID 문자열 패턴 전수 grep으로 대상 파일 종류를 확정하고, 재채번
  후 "미해석 GUID 0" 검사를 돌린다. 모델 바이너리 캐시는 재생성이 싸므로
  전체 무효화로 푼다.
- **쿠킹 왕복 검사의 사각** — 리플렉션 밖 수기 파싱(ComponentFactory 27곳)이
  D3에서 완전히 수렴하지 않으면 왕복 비교가 그 필드를 못 본다. D3 판정에
  "수기 접근 잔존 목록 0 또는 명시 예외 표"를 포함한다.
- **`[[Property]]` 리플렉션 등록 누락 필드** — 씬 YAML에는 있는데 리플렉션
  테이블에 없는 필드가 있으면 쿠킹에서 조용히 탈락한다. 왕복 검사가 YAML
  키 집합과 리플렉션 프로퍼티 집합의 차집합을 보고하게 한다.
- **트랙 P와의 경합** — 프리팹 오버라이드(P1~P4)가 진행 중이면 D3의
  DeserializePrefab 교체와 충돌한다. 착수 전 트랙 P 상태 확인, 겹치면 P 우선.
