# 직렬화 이원화 — 저작 텍스트 · 런타임 쿠킹 바이너리 (PHASE 17)

2026-08-16. "yaml이 무겁고 느린 것 같다 — 기존 구조 호환을 신경 쓰지 않는다면
어떻게 리팩토링할 것인가"라는 물음에서 출발했다. 2026-08-22에는 **라이브러리
최소화·성능 우선·추후 네트워크 프레임워크 편입** 요구를 반영해 개정했다. 결론은
"YAML을 무엇으로 교체하나"(일차원)가 아니라 **저작·런타임·wire 소비자를 같은
스키마 위의 서로 다른 Archive로 분리한다**는 것이다. 목표 사슬:

```
에디터 저작(rapidyaml/YAML 1.2, git-diff 가능)
    → AuthoringArchive
    → CookedArchive(리플렉션 순회 → 바이너리 + GUID 매니페스트)
    → pak → Player/Server(바이너리 로드만, 텍스트 parser 호출 0)

meta::schema
    → NetworkArchive(복제 opt-in 필드만 → bitstream)
    → NetReplication → Transport
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

### 1.6 2026-08-22 재감사 — 네트워크 경계를 막는 현재 표면

PowerShell 전수 검색을 다시 돌린 현재 값은 YAML 43개 소스 파일·268 matches,
`nlohmann::json` 16개 소스 파일·44 matches다. 장기 보관 Node도 기존 문서의 3곳이
아니라 다음 4곳이다.

| 상주 지점 | 현재 용도 |
|---|---|
| `Prefab::m_prefabData` (`Prefab.h:73`) | Prefab 원본 문서 |
| `Entity::m_prefabOriginal` (`Entity.h:381`) | 오버라이드 시딩용 임시 원본 |
| `GameObjectCommand::m_serializedNode` (`GameObjectCommand.h:106`) | Undo/Redo snapshot |
| `SceneManager::m_editorSceneBackup` (`SceneManager.h:169`) | Editor scene backup |

또한 `ReflectionYml.h:31`의 `namespace MetaYml = YAML` 별칭, YAML iterator를 받는
`ComponentFactory`, JSON 타입을 virtual interface에 박은 `ISerializable`이 남아 있다.
단순 parser 교체로는 이 표면이 NetCore/Server target에 그대로 전이된다. D3는 이제
Node 이름만 감추는 단계가 아니라 **문서 소유권과 Archive 소비자를 분리하는 단계**다.

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

### 3.1 저작 포맷 — rapidyaml 기반 YAML 1.2로 통일

텍스트 문법은 YAML로 유지하되 parser/DOM backend는 yaml-cpp에서
**rapidyaml(ryml)**로 바꾼다. 근거:

1. 저작물의 가치는 git diff/merge·충돌 해결·손상 시 수동 복구·텍스트 검색에
   있고, 저작 시점 성능은 병목이 아니다(1.1).
2. MaterialPipelinePlan §3.2가 `.shader` DSL을 **YAML로 통합**하기로 이미
   결정했고("파서를 하나 더 유지할 이유가 없다"), SceneGraphRedesignPlan §5의
   포맷 예외 1~5가 전부 YAML 위에 설계됐다. 저작 포맷 교체는 두 계획을 다시
   여는 비용 대비 얻는 것이 없다.
3. ryml은 mutable flat tree와 YAML/JSON parse·emit을 한 backend에서 제공하므로
   yaml-cpp+nlohmann의 이중 DOM을 줄일 수 있다.
4. 성능 문제의 최종 답은 여전히 **런타임에서 텍스트 parser를 치우는 것**(3.2)이다.
   ryml 전환은 Editor 저장·로드와 dependency/build 비용을 줄이는 별도 가치다.

새 authoring 정본은 YAML 1.2다. 기존 JSON 파일은 D4 동안 ryml JSON parser로 읽고
YAML로 재저장한다. 외부 교환 계약이 생긴 JSON만 명시 예외로 남긴다. `.asset` 확장자는
텍스트와 모델 binary cache가 혼재하므로 확장자 일괄 변환은 금지하고, 실제 text YAML
문서만 변환한다. ryml의 amalgamated 구성은 별도 c4core package를 피할 수 있지만
포함 코드와 라이선스/SBOM은 ryml+c4core로 기록한다.

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

이 규칙은 network wire에는 적용하지 않는다. 쿠킹 산출물은 배포 전에 전량 재생성할
수 있지만, wire는 서로 다른 프로세스가 동시에 해석한다. PHASE 20은 별도
`ProtocolVersion + NetSchemaHash + stable ID` 계약을 사용한다.

### 3.3 Document/Archive 격리 — 소유권과 소비 정책을 분리

ryml의 `NodeRef`는 `Tree`를 소유하지 않는다. 따라서 `MetaYml = ryml` 같은 별칭
교체는 금지한다. 장기 보관 root는 `AuthoringDocument`가 `ryml::Tree`를 소유하고,
내부 탐색은 문서 수명 아래의 `AuthoringNodeView`/node id로 제한한다. 장기 문서는
`parse_in_arena()`를 쓰며, `parse_in_place()`는 입력 buffer 수명이 명시된 단기
경로에서만 허용한다.

리플렉션 소비는 다음 셋으로 분리한다.

| Archive | 역할 |
|---|---|
| `AuthoringArchive` | map/sequence/scalar, Editor generic edit, 구조 비교 |
| `CookedArchive` | runtime 저장 필드의 keyless binary |
| `NetworkArchive` | PHASE 20의 `replicated_attr` opt-in 필드와 bitstream |

`DeserializePrefab`의 Dump 문자열 diff는 **구조적 동등성 비교**로, `YAML::Clone`은
명시적 subtree duplication으로 교체한다. `YAML::Binary`는 backend 암묵 동작에 기대지
않고 명시적인 base64 scalar codec으로 만든다. 이 단계 뒤에는 Entity/ComponentFactory가
YAML/JSON 타입을 알지 않는다.

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

### 3.5 JSON 트랙 정리 — nlohmann 제거

- **애니메이터(Y-5)**: 씬 YAML 리플렉션 경로를 단일 진실로 하고 에디터 `.json`
  별도 저장을 은퇴시킨다. 빈 `Deserialize()`(Y-7)도 함께 제거한다.
- **`ISerializable`**: `nlohmann::json` 고정 virtual 계약을 Archive/typed value 계약으로
  교체한다. Runtime interface에 generic text DOM을 두지 않는다.
- **입력맵·터레인 본문**: ryml JSON parser로 기존 파일을 읽는 migration reader를
  먼저 세운 뒤 YAML 1.2로 재저장한다. migration 종료 뒤 dual-write와 nlohmann
  consumer를 제거한다.
- 외부 서비스가 요구하는 JSON은 PHASE 20 control/backend adapter 소관이며 realtime
  replication format으로 승격하지 않는다.

### 3.6 쿠킹과 pak 매니페스트

BuildPipelinePlan §2.3의 Cook 단계(현재 hlsl→cso만)에 씬·프리팹·`.asset`
쿠킹을 추가한다 — 같은 계획 §4가 "에셋 포맷 쿡"을 의도적으로 범위에서
제외하며 남겨둔 자리(AssetResidencyPlan 연계 예정)를 이 문서가 채운다.
pak에는 매니페스트(GUID → 가상 경로/오프셋)를 함께 굽고, Player의
`AssetMetaRegistry`는 매니페스트에서 직접 구축된다 — 부팅 스캔·efsw·pak 내
`.meta` 동봉이 전부 불필요해진다.

#### 3.6.1 파생물은 콘텐츠가 아니다 — Content / Derived 분리 (2026-08-25 결정)

쿠킹 산출물이 **콘텐츠 서브트리 안에** 놓이는 현재 배치를 끝낸다.

**증상.** 원본과 파생물이 같은 폴더에 섞여 있어 `.gitignore`가 규칙 하나로 못
가른다 — `Prim_Cone.asset`(파생)과 `Gunner_F_Mythic.glb`(원본)이 같은 줄에
걸린다(`.gitignore:482`). 그래서 폴더마다 손으로 allowlist를 쓰고 있고, 콘텐츠가
늘수록 그 누더기가 커진다.

**이미 자리는 나 있다.** `PathFinder`에 `CacheRoot`(= `RuntimeDataRoot / "Cache"`)와
접근자가 정의되어 있는데 **소비자가 0**이다. 루트 이음새도 이미 있다 —
`EnginePaths`가 `projectRoot`·`assetsRoot`·`runtimeContentRoot`·`runtimeDataRoot`·
`engineResourceRoot`를 나눠 들고 있다. 새로 만들 것이 아니라 쓰지 않고 있을 뿐이다.

| 층 | 내용 | 위치 | git | 쓰기 |
|---|---|---|---|---|
| Engine | 엔진·에디터 바이너리, 기본 리소스 | `engineResourceRoot` | — | 읽기 전용 |
| **Content** | 원본 + `.meta` | `assetsRoot` | 추적 | 사람이 |
| **Derived** | 쿠킹 `.asset`·`.cso`·임포트 캐시 | **`CacheRoot`** | 통째 무시 | 엔진이 |

**Derived 안은 GUID로 주소를 매긴다** — `Cache/Model/<guid[0:2]>/<guid>.asset`.

이유가 성능이 아니라 **정체성**이다. 지금은 두 군데서 평탄화한다: 쿠킹 경로가
`Models\<stem>.asset` 고정이고, §1.5대로 GUID 자체도 파일명만 해시한다. 실측
(2026-08-25, 프로젝트 자산 275개): stem 충돌 17건, 그중
`AnimatorController/MonsterC.json`과 `NodeEditor/MonsterC.json`은 **서로 다른
자산인데 같은 GUID**다.

UE의 `.uasset`을 그대로 들여오지는 않는다 — 그쪽은 파생물이 아니라 **정본**이라
자산 타입마다 저작 표면이 필요하고, §3.4에서 이미 Unity형(원본 정본 + sidecar)을
택했다. **가져오는 것은 형태가 아니라 두 규칙이다: "파생물은 콘텐츠가 아니다"와
"정체성은 평탄화하지 않는다".**

**순서 — §3.4가 선행이다.** GUID가 유일해지기 전에 GUID로 주소를 매기면 파일명
충돌이 캐시 충돌로 옮겨갈 뿐이고, 그때는 원인이 더 안 보인다. 그때까지 쿠킹
산출물의 경로를 만드는 지점은 **헬퍼 하나로 모아 두기만 한다**
(ModelImportPipelinePlan I7 이 이 결정을 따르며 경로 규약을 발명하지 않는다).

★ legacy가 이미 밟은 함정 — 쓰기·읽기는 `Models\` 고정인데 **사용 판정만 원본
옆을 본다**(`Model::LoadModel`). `Assets/Models/` 밖의 모델은 쿠킹이 있어도
탈락한다. 실측: `Animation/Ani_Mon_3_die.fbx`는 `Models/Ani_Mon_3_die.asset`
(1.01MB)이 있는데도 매번 Assimp 68.9ms를 돈다. 경로를 만드는 지점이 갈라지면
반드시 이렇게 어긋난다.

### 3.7 네트워크 이음새 — 포맷 공유가 아니라 schema visitor 공유

PHASE 17은 socket, packet, RPC, replication을 구현하지 않는다. 대신 PHASE 20이
포맷 결합 없이 시작할 수 있도록 다음 경계를 완료 조건에 포함한다.

- `MetaSchema`는 std-only canonical descriptor를 유지한다.
- 저장 필드는 기존 선택 규칙, network 필드는 `replicated_attr` opt-in으로 갈라진다.
- `EntityHandle`, `size_t typeID`, Node view는 wire type으로 직렬화할 수 없다.
- `ComponentFactory`는 `Meta::Type`/정규화된 descriptor를 받고 authoring adapter가
  textual type을 해석한다.
- scalar codec은 공유할 수 있지만 CookedArchive와 NetworkArchive의 header/version/
  delta/quantization 정책은 공유하지 않는다.

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
★ 대규모 리팩터링의 정책은 **기존 GUID 호환 없음**이다. 기존 UUIDv4도 보존하지
않고 sidecar 226개를 전부 재발급하며, old→new 표는 현재 저작 자산을 같은
transaction에서 고치기 위한 일회성 ledger일 뿐 runtime compatibility table이 아니다.
판정: 동명 파일 2개가 서로 다른 GUID · 파일 리네임 후 씬 로드 시 참조 유지 ·
전체 씬 14종 로드 왕복 diff 0(재채번 직후 1회 재저장 기준).

**D2-a — sidecar 전수 감사 + catalog 충돌 fail-closed — ✅ 구현·표적 검증 완료
(2026-08-29).** 실제 재채번에 앞서
`Tools/regression/verify-asset-guid-contract.ps1`을 추가했다. 생성/빌드/임시 출력
디렉터리를 제외한 sidecar 226개를 읽어 GUID 문법·UUID version·중복·대상 파일·Git
추적 여부를 한 줄로 출력하며, 기본 audit는 현황을 기록하고 `-Strict`는 D2 최종
계약을 만족하지 않으면 실패한다. 현재 기준선은 parsed 226, invalid 0,
missing-target 0, duplicate GUID 0, UUIDv4 14, non-v4 212, tracked 29,
policy-untracked 197, `d2Ready=false`다. 따라서 이 절편은 자산을 재작성하거나
현재 deterministic v5 GUID를 완료로 승격하지 않는다.

`AssetMetaRegistry::Register`는 이제 `Registered`·`AlreadyRegistered`·`Invalid`·
`GuidConflict`·`PathConflict`를 반환한다. 같은 GUID가 다른 path를 가리키거나 같은
path가 다른 GUID를 받으면 기존 양방향 매핑을 지우지 않고 거부하며 DataSystem이
guid/path/기존 매핑을 오류로 남긴다. idempotent 재등록은 허용한다. selftest는 두
충돌 방향과 invalid 입력을 넣어 거부 뒤 첫 매핑 보존을 단정한다.

VS18 MSBuild/v145 Debug x64에서 RenderTests·CreatorEditor·Player가 성공했고
`dx12.selftest`는 새 catalog 계약을 포함해 통과, stderr 0이었다.

**D2-b1 — UUIDv4 발급 계약 + migration transaction manifest — ✅ 구현·표적 검증
완료 (2026-08-29).** `FileGuid::CreateRandomV4()`는 경로나 이름을 받지 않고
`CoCreateGuid` 결과를 RFC byte order로 옮긴 뒤 version 4·variant bit를 명시한다.
selftest는 nil 아님·연속 발급 충돌 없음·version/variant·문자열 왕복을 단정하며
D2-a의 catalog 충돌 검사와 함께 실행된다.

`Tools/migration/New-AssetGuidMigrationManifest.ps1`은 자산 트리를 쓰지 않는 준비
단계다. sidecar·대상 파일·GUID 유일성을 다시 검사하고 현재
`metaPath/oldGuid` 인벤토리 SHA-256을 봉인한 뒤 기존 version과 무관하게 전부 새
UUIDv4로 매핑한다. 알려진 참조는 `.creator/.asset/.prefab/.meta`에서만 세어
entry별 occurrence/file 목록으로 남기며 source file이나 임의 UUID 문자열은
재작성 대상에 넣지 않는다. 참조 path/GUID occurrence도 별도 SHA-256으로 봉인한다.
manifest는 `Dynamic_CPP/Assets` 밖에만 만들고 기존 파일을 덮어쓰지 않으며
`-ValidateExisting`은 meta·reference 인벤토리 drift, entry별 source mapping,
mapping 중복과 UUIDv4 계약을 재검증한다.

호환 제거 정책과 binary cache 감사를 반영해 manifest schema는 v3로 올렸고 이전
schema reader를 남기지 않았다. CEMA v2 모델 `.asset` 14개 안에는 YAML ASCII GUID
165건뿐 아니라 raw `Uuid16` 3건도 있었으므로 둘을 서로 다른 reference kind로
봉인한다. 현재 dry-run은 assets 226, regenerate 226, preserve 0, known reference
occurrence 431,
inventory SHA-256
`cdb2210ebf359630fd35a4c2763ed65c434a64d322734590cdb464478ec4cd12`, asset write
0이었다. reference inventory SHA-256은
`5abac0111533a9bd226cd16bcec8afdbd56ac894bb8625ba1d6b96199f3d3c8f`다. 생성 직후
validate exit 0, 새 GUID 226개 유일, old==new 0, asset size/timestamp drift 0을
확인했다.

Editor의 GUID 없는 신규 sidecar fallback도 파일명 UUIDv5에서 random UUIDv4로
전환했다. 기존 sidecar가 있으면 `.meta`만 identity 정본이고 payload의 `m_fileGuid`는
mirror다. sidecar가 없는 최초 저작 publication에 한해서만 prefab/material payload의
UUIDv4를 생성 힌트로 받으며, 구형 prefab 첫 엔티티의 `m_prefabFileGuid`를 복구하는
호환 분기는 제거했다. 현재 prefab 9개는 모두 루트 `m_fileGuid`를 가져 제거된 분기의
소비자가 0이다.
VS18/v145 CreatorEditor·Player 빌드와 `dx12.selftest`(stderr 0)도 통과했다.

**D2-b2 — 전수 재발급 transaction apply + Git pair 정책 — ✅ 구현·적용·표적 검증
완료 (2026-08-29).** `Tools/migration/Invoke-AssetGuidMigration.ps1`은 먼저 기존
manifest validator를 통과시킨 뒤 254개 영향 파일을 외부 backup/staged 트리에
복사한다. UTF-8 문서는 문자열로, binary `.asset` 14개는 ASCII 36-byte와 raw
16-byte 패턴으로 나눠 동일 길이 치환하며, entry별/전체 치환 수·old GUID 잔존 0·
source hash drift·post-copy hash를 모두 검사한다. stage-only에서 254 files,
14 binary, 431 replacements, asset writes 0을 확인한 뒤 같은 계약으로 실제 적용했고
28,538,243-byte 원본 254개를 저장소 밖 backup에 남겼다.

적용 후 live sidecar는 UUIDv4 226, non-v4 0, invalid/missing/duplicate 0이다.
2026-08-29 프로젝트 분리 전 Git 경계를 다시 좁혔다. `*.meta`는 기본 ignore이고,
저장소가 소유하는 clean-checkout 폐포(Prim/추적 FBX model, 대표 BaseColor,
Forward material, ShaderMeta, 기존 IBL)의 target+meta만 `.gitignore`에 명시적으로 예외한다.
자동 생성된 일반 HLSL/Volume/HDR 등 sidecar 70개는 `git add -f`로 강제 추가하지
않고 로컬에만 보존한다. `verify-asset-guid-contract.ps1 -Strict`는 Git이 추적하는
`.meta`가 현재 ignore 정책 밖의 명시적 예외인지 확인하며, `git add -f` 우회를
`tracked-meta-policy-violation`으로 거부한다. 예외 target에만 sidecar를 필수로 하고
orphan tracked meta를 거부하며 `d2Ready=true`를 유지한다.
현재 index/live corpus 판정은 tracked meta 38, policy violation/required-meta-untracked/
orphan 0, ignored-meta-with-tracked-target 70, local target/meta pair 118이다.

VS18/v145 CreatorEditor·Player 빌드, `dx12.selftest`, `experiment.gltf
Prim_Cube.glb`, `experiment.cooked Prim_Cube.glb` 실자산 왕복이 stderr 0으로
통과했다. 당시 `dx12.scene` draw 0은 보관 원본 254개를 잠시 복원한 A/B에서도
동일했고 이행본 재복원 hash drift가 0이라 GUID 이행 회귀에서는 분리했다. 후속
D2-c에서 fixture 자체가 아니라 검증 시점의 RenderThread 진행도 차이로 확정했다.

**D2-c — sidecar authority + atomic authoring/rename + 실제 draw gate — ✅ 구현·표적
검증 완료 (2026-08-29).** 영속 asset ID의 경로/이름 기반 생성 API
(`CreateFromName`, `MakeFileGUID`, `make_file_guid`, filesystem namespace UUID)를
제거했고 legacy bridge의 `.meta` 누락 path-v5 fallback도 fail-closed로 바꿨다.
Material/Prefab 저장은 payload를 먼저 노출하지 않고 Editor authoring lock 안에서
temporary file publication과 UUIDv4 sidecar 생성을 한 transaction으로 수행한다.

rename은 sidecar를 먼저 옮긴 뒤 target을 옮기며 target 이동 실패 시 sidecar를
rollback한다. `asset.guid.rename.probe`는 material 저장→target/meta rename→catalog
역조회→material YAML 재직렬화→cleanup을 수행해 같은 UUIDv4와 구조 dump를 단정한다.
`verify-asset-guid-rename.ps1`와 prefab 저장·재로드 왕복은 각각 통과했다.

`FT_Primitives`의 draw 0은 headless `wait 120`이 frame 124개를 빠르게 발행하는 동안
RenderThread가 6개만 소비하고 118개를 latest-wins로 합친 상태에서 검증이 새 씬의
create delta 적용 전에 `RenderScene`을 읽은 오진이었다. 일반 프레임은 그대로
비동기이며, 오프라인 검증 진입에만 bounded queue drain API를 두었다.
`verify-experiment-ft-primitives.ps1`은 같은 wait 120 경로에서 draw candidate 8,
light 1, draw 8, mesh upload 8을 단정해 통과한다. strict 계약은 UUIDv4 226,
required/orphan 0, `d2Ready=true`이고 VS18/v145 Debug x64 CreatorEditor도 통과했다.

**D2-d — 전체 authoring corpus gate — ✅ 구현·전수 검증 완료
(2026-08-29).** 현재 코퍼스는 scene 14, prefab 9, standalone material asset 2다.
`verify-scene-authoring-corpus.ps1`은 원본을 입력으로만 열고 외부 temporary tree의
동명 경로에 1차 저장→재로드→2차 저장한다. 14개 전수에서 load 28/28, save 28/28,
1차/2차 SHA-256 불일치 0, 누락 0, 원본 hash 변경 0, 예상 밖 stderr 0으로 통과했고
전체 회귀 세트에 편입했다. temporary 산출물은 실패 진단을 위해 보존한다.

`verify-prefab-authoring-corpus.ps1`은 9개 전부를 외부 임시 씬에 소환해 13개 prefab
entity, override 1건, 등록 root 11건의 identity/override multiset을 저장 전·재로드 후
같은 digest로 확인했다. 소환 9/9, 원본 target/meta hash 변경 0, 예상 밖 stderr 0이다.

`verify-material-authoring-corpus.ps1`은 standalone material 2개를 메모리에서 두 번
왕복해 sidecar 정본과 payload mirror identity, ShaderMeta catalog 해석, texture reference,
canonical payload를 확인했다. 착수 감사에서 두 payload의 `m_fileGuid`가 sidecar와 다른
실제 위반을 발견해 sidecar 값으로 정규화했고, 이후 2/2 통과·원본 실행 중 변경 0·예상
밖 stderr 0이다. 현재 두 재질의 non-nil texture reference는 0개이며 ShaderMeta reference
2개는 모두 catalog target으로 해석된다. UUID version 판정은 중복하지 않고 전역 strict
게이트가 맡는다.

세 corpus gate와 전역 strict 계약을 `run-all.ps1`에 편입했다. live corpus 결과는 UUIDv4
226/non-v4 0/invalid·missing·duplicate 0이고, Git 정책 gate는 명시적 target/meta
폐포만 추적하며 강제 추가 violation 0, `d2Ready=true`다. GUID rename,
FT_Primitives 실제 DX12 draw와 세 corpus gate가 모두
통과했고 VS18/v145 Debug x64 CreatorEditor·Player 빌드도 성공했다. 이로써 D2는 완료다.
old→new ledger와 외부 backup은 제품/runtime compatibility 경로에 남기지 않는다.

**D3-a — format-neutral Document/Archive 경계 (Y-6, 3일)**
장기 보관 4곳 + SceneManager 시그니처 + ComponentFactory 수기 접근을 3.3의
문서/Archive 경계 뒤로 옮긴다. 이 슬라이스는 yaml-cpp backend를 유지하는 동작 불변
리팩터다. Dump 문자열 diff → 구조 비교, `ComponentFactory::LoadComponent` →
정규화된 `Meta::Type` 생성/적용 경계로 바꾼다. 판정: Entity/ComponentFactory와
Runtime interface의 YAML Node 타입 0 + 회귀·왕복 검사 통과 + D0 대비 비퇴행.
`ISerializable`의 JSON 고정 계약은 D4가 닫는다.

**D3-b — rapidyaml backend 전환 (2일)**
`AuthoringDocument`의 backend를 ryml `Tree`로 바꾸고 장기 문서는
`parse_in_arena()`로 소유한다. Clone/remove/dump/binary 사용부를 subtree duplicate,
구조 비교, 명시 base64 codec으로 옮긴다. yaml-cpp Release/Debug DLL 패키징을 제거한다.
판정: 전 authoring 문서 로드·재저장·Prefab/Undo/Editor generic node edit 통과 +
yaml-cpp consumer/include 0 + D0과 동일 workload의 Release A/B 첨부.

**D4 — structured text 통일과 nlohmann 제거 (Y-5·Y-7, 2일)**
3.5. 애니메이터 단일 진실화, 입력맵·터레인 migration reader, `ISerializable` Archive
전환이 본체다. 판정: 애니메이터 저장 경로 1개 · dual-write 0 · nlohmann consumer/
include 0 · 기존 JSON fixture read 후 YAML canonical save · 상태 그래프/입력/터레인
회귀 통과.

**D5 — 쿠킹 바이너리 + pak 매니페스트 (4일)**
리플렉션 순회 바이너리 라이터/리더(3.2) + Cook 단계 확장 + 매니페스트(3.6).
Player 로드 경로를 매니페스트+바이너리로 전환하고 `.meta`를 pak에서 제외.
★ 최대 위험 슬라이스 — "배선만 이으면 그림이 더 나빠진다"(Forward+ 전례)를
직렬화판으로 반복하지 않기 위해, **쿠킹 왕복 검사**(YAML 로드 결과 ==
바이너리 로드 결과, 리플렉션 프로퍼티 전수 비교)를 먼저 세우고 배선한다.
판정: 왕복 검사 전 씬·전 프리팹 통과 · Player 씬 전환 시간 D0 대비 수치
개선(목표치는 D0 결과를 보고 이 문서에 추가) · 게임 빌드 실플레이 확인.

**D5-a — Experiment model cooked identity publication gate — ✅ 구현·표적 검증 완료
(2026-08-29).** 기존 cooked V3가 model/material/ShaderMeta/texture `AssetId` 필드를
직렬화하면서도 producer가 material ID를 채우지 않고 texture path fallback을 그대로
구울 수 있던 단절부터 닫았다. `ConversionOptions`는 catalog/authoring 쪽에서 발급한
material identity를 받는 `resolveMaterialAsset`을 제공하고, 기존 texture resolver와 함께
변환 결과에 싣는다. resolver가 비었거나 nil을 반환한 source preview draft 자체는 허용하지만
`CookedModelCodec::Write`가 publication gate가 되어 nil model/material/ShaderMeta/texture ID와
payload 내부 model/material owned-ID 충돌을 `Error` issue로 남기고 빈 bytes로 거부한다.
fallback path는 cooked texture identity를 대신하지 못한다.

호출부는 실패 가능한 `CookedWriteResult`를 소비하도록 전환했다. 합성 게이트는 nil model,
nil material, nil ShaderMeta, fallback path만 있는 texture, 중복 material ID 5종을 모두
거부하고 정상 V3 왕복·결정성을 포함해 310/310을 통과했다. `Prim_Cube.glb` 실자산은
fixture 전용 resolver로 model/material/shader/texture identity를 각각 1/1 채워 24 vertices,
2,480 B, 52/52 왕복을 통과했다. 이는 **제품 catalog 배선 증거가 아니라 codec 경계
증거**다. `verify-experiment-cooked-identities.ps1`은 두 CLI pass, 음성 게이트 2회,
원본 target/meta hash 변경 0, 예상 밖 stderr 0을 확인하며 `run-all.ps1`에 편입됐다.
VS18/v145 Debug x64 CreatorEditor·Player 빌드도 성공했다.

**D5-b1 — model subasset identity + GUID-addressed binary manifest contract — ✅ 구현·표적
검증 완료 (2026-08-29).** 모델 sidecar에 `subAssets.schemaVersion: 1`과
`materials[]`/`embeddedTextures[]`의 `sourceKey → UUIDv4` 매핑을 두었다. glTF는
`gltf/material/<index>`·`gltf/image/<index>`, FBX는 `fbx/material/<typed_id>`를 게시하지만
이 키 자체를 ID로 해시하지 않는다. 이름은 진단용일 뿐 identity가 아니며, 누락·중복·stale
key와 nil/non-v4/비정규 UUID 표기는 fail-closed다. UUID 판정은 별도 전수 게이트를 만들지 않고
기존 `verify-asset-guid-contract.ps1`에 nested subasset 2개와 최상위 226개의 전역 충돌 검사를
합쳤다(`invalidSubasset=0`, duplicate 0, `d2Ready=true`).

`CookedAssetManifest` CEMF v1은 UUID 순으로 정규화한 binary writer/reader와
`GUID → kind/formatVersion/Derived path/byteSize/SHA-256/dependency GUID` 계약을 제공한다.
model path는 `Derived/Models/<앞2자리>/<guid>.cemc`이고 절대 경로·역슬래시·dot segment,
nil/중복/non-v4 ID, 빈 digest, self/duplicate/missing dependency, stale size/hash를 거부한다.
`ConversionOptions::resolveShaderAsset`도 추가해 opaque/mask→GBuffer, blend→Forward의 실제
ShaderMeta sidecar ID를 재질별로 공급한다. `Prim_Cube.glb`는 fixture ID 없이 실제 sidecar의
model/material/embedded texture와 GBuffer ShaderMeta ID를 사용해 24 vertices, 2,480 B,
64/64 왕복을 통과했고 model+material 2-entry manifest lookup/SHA-256도 왕복했다. 합성은
331/331, subasset 음성 4/4와 manifest 음성 6/6을 두 CLI pass에서 모두 통과했으며 원본
변경/stderr는 0이다. VS18/v145 Debug x64 CreatorEditor·Player도 성공했다(기존 PhysX PDB와
Vulkan delay-load linker warning만 유지).

**D5-b2a — 별도 AssetCooker + 단일 실제 model 원자 게시 — ✅ 구현·표적 검증 완료
(2026-08-29).** `AssetPacker`에 import/RenderEngine 책임을 얹지 않고 별도 VS18/v145
`Tools/AssetCooker`를 추가했다. `ModelCookProducer`는 source/.meta와 GBuffer/Forward ShaderMeta
sidecar를 읽어 model/material/embedded texture identity를 해석하고 checked CEMC와 model+material
CEMF entry를 완전 소유 값으로 만든다. 도구는 하나 이상의 `--model` product를 새 staging tree에
모아 artifact와 `Derived/asset-manifest.cemf`를 다시 읽고 GUID/크기/SHA-256/subasset lookup을
검증한 뒤 디렉터리 rename 한 번으로 게시한다. 기존 output, `Assets` 내부 output, invalid UUID
sidecar는 partial output 없이 거부한다. 이 링크 경계를 위해 experiment import/cooked TU를
RenderEngine unity object에서 분리했으며 렌더러 전체를 도구에 끌어오지 않는다.

`verify-experiment-asset-cooker.ps1`는 `Prim_Cube.glb`를 외부 임시 tree에 두 번 cook해 두 결과
hash가 같고 CEMC 2,432 B·CEMF 318 B·2 entry인지 확인했다. 같은 Assets tree를
다른 물리 경로에 복제해도 CEMC/CEMF hash가 같다. 기존 output/Assets 내부 output/
손상 sidecar 음성도 모두 통과했고 partial output 0, 원본 model/meta/ShaderMeta 변경 0,
성공 stderr 0이다. VS18/v145 Debug x64 AssetCooker·CreatorEditor·Player 빌드도 성공했다
(기존 ScriptCore trim/analyzer, PhysX PDB, Vulkan delay-load warning만 유지).

**D5-b2b1 — 모델 전수 sidecar 재발급 + 전수 Cook — ✅ 구현·전수 검증 완료
(2026-08-29).** 제품 Cook은 source sidecar를 수정하지 않는다. 별도 authoring 경계인
`AssetCooker --refresh-model-identities`가 asset root의 최상위/기존 subasset UUIDv4를 먼저 전부 예약하고,
현재 import 결과에서 실제로 소비되는 material/embedded texture source key에만 충돌 없는 새 random UUIDv4를
발급한다. 전체 import/YAML 자기 검증이 끝난 뒤 sibling temporary를 준비하고 일괄 replace하며,
게시 중 실패하면 이미 바꾼 sidecar를 원문으로 rollback한다. legacy subasset ID 보존/fallback은 없다.

현재 checkout의 tracked 11 + local 3, model 14개 sidecar를 이 경계로 전수 재발급했다.
material 52·embedded texture 96,
model을 포함한 corpus ID 162개는 전부 canonical UUIDv4이고 중복 0이다. 전체 저장소 gate도
`meta=226`, `subassetGuids=148`, invalid/missing/duplicate 0, `d2Ready=true`를 유지한다.
`verify-experiment-model-cook-all.ps1`는 14개를 두 번 cook해 각각 CEMC 14개 + CEMF 1개,
manifest entry 66개, CEMC 합계 17,752,200 B, CEMF 10,030 B를 만들고 tree hash 동등,
원본 model/meta 변경 0, 성공 stderr 0을 확인한다. 복제 `Prim_Cube`에서는 명시적 refresh가
상위 GUID를 유지하고 하위 GUID 2개만 새 UUIDv4로 바꾸는 것도 검증한다. VS18/v145 Debug x64
AssetCooker 빌드와 기존 단일 model gate도 통과했다.

**D5-b2b2 — build/AssetPacker/pak 통합 — ✅ 구현·실제 패키지 검증 완료 (2026-08-29).**
`Tools/build.ps1`은 `InputMode` 규약으로 먼저 외부 `package-input/Base`를 만들고, live project가
아닌 그 snapshot의 `Assets`를 `AssetCooker`에 넘겨 `Generated/Assets/Derived`를 만든다.
저작 입력의 기존 `Assets/Derived`는 stale 산출물로 거부하고, Cook 결과만 `Merged`에
overlay한다. `-BuildNative`는 Player·AssetPacker·AssetCooker를 VS18/v145로 함께 빌드하며,
세 바이너리 모두를 필수 입력으로 판정한다.

Cook 전·merge 후에 GUID-addressed CEMC 수가 model 수와 같고 CEMF가 딱 하나인지 재검증한다.
`package-manifest.json` 안의 `cook` 섹션은 producer/source/artifact root/model·artifact·byte 수와
CEMF SHA-256을 기록한다. `ModelCookProducer`는 CEMC provenance를 asset-relative 논리 경로로
고정하고 물리 cooked path와 mtime을 직렬화 경계에서 제거해 staging 위치 불변을 보장한다.

Debug/Project/SkipVerify 실제 패키징은 model 14, CEMC 14 + CEMF 1, entry 66,
CEMC 17,752,200 B, CEMF 10,030 B를 전수 gate와 동일하게 만들었다. 분배 pak은
529 sorted entries/510,108,259 B였고 `AssetPacker`의 reopen/index exact path·size 검증을 통과했다.
`SkipVerify`이므로 candidate는 publish하지 않았고 Player runtime 소비 증거로 계산하지 않는다.
build는 identity-refresh를 호출하지 않으며 sidecar 불일치는 fail-closed다.

**D5-b2c — texture/ShaderMeta/scene/prefab producer — ◐ 진행.** 이 종류의 Derived artifact와
material의 shader/texture dependency entry를 완성해야 D5-b 전체가 닫힌다. legacy cooked cache
호환은 두지 않고 전수 재쿠킹한다. 따라서 제품 Cook/pak 배선은 아직 완료가 아니다.

분할한다. 하나로 묶으면 어느 producer가 폐포를 깼는지 못 가른다.

| 슬라이스 | 내용 | 상태 |
|---|---|---|
| **b2c-1** | texture producer + manifest entry | ✅ 2026-08-29 |
| **b2c-2** | ShaderMeta producer + shader/texture dependency | ⏳ |
| **b2c-3** | material dependency 배선 + standalone material 2 | ⏳ |
| **b2c-4** | scene 14 · prefab 9 producer | ⏳ |
| **b2c-5** | 전체 GUID 폐포 fail-closed + AssetPacker/pak 게시 | ⏳ |

**D5-b2c-1 — texture producer — ✅ 구현·전수 검증 완료 (2026-08-29).**
`TextureCookProducer`가 texture `.meta`의 canonical UUIDv4만 identity로 삼아
`Derived/Textures/<앞2자리>/<guid><ext>` artifact와 `kind=Texture` manifest entry를
만든다. `AssetCooker`에 `--texture`를 추가했고 model과 같은 staging/원자 게시 경로를
탄다. `ReadTextFile`/`IsContainedPath`/`ReadMetaAssetId`는 `CookSupport`로 뽑았다 —
producer마다 익명 namespace에 복제하면 MSVC 유니티 빌드가 그 익명 namespace들을
합치면서 곧바로 재정의가 되고, 더 나쁘게는 `.meta` 판독 규칙이 producer마다 갈라진다.

★ **artifact는 원본 바이트 그대로다. 트랜스코딩하지 않는다.** BC7·밉은 압축기를
들이는 별개 작업이고, 지금 넣으면 D5-b2c가 거기 묶인다. B3가 셰이더에 대해 이미 같은
자세를 취한다. 이 슬라이스가 실제로 주는 것은 압축이 아니라 **주소 체계**다 — GUID
주소·내용 해시·manifest 등재. 그것이 D5-c가 필요로 하는 것이고, `formatVersion`이 1이라
트랜스코딩이 들어오는 날 2가 되며 구버전 artifact는 자동으로 거부된다.

실측: corpus texture 119개 중 **112개 cook 성공**(png 98 · hdr 14 · **dds 0**),
323,863,479 B, manifest entry 112, 두 번 cook한 tree hash 동등, stderr 0. model 14개와
함께 구우면 entry 178(model 14 + material 52 + texture 112), CEMC 합계 17,752,200 B로
D5-b2b2 수치가 그대로 재현됐다 — texture 편입이 model 산출물을 바꾸지 않는다.

★ **나머지 7개는 fail-closed로 거부됐고, 그게 이 슬라이스가 찾아낸 결함이다.**
`ColorGrading/LUT_3.png` · HDR 5개 · `VolumetricFog/blueNoise.dds`의 sidecar가
`{...}` brace 표기를 쓴다. `AssetIdentity.h`는 legacy 표기 호환을 두지 않으므로
producer가 거부한다. 그런데 `verify-asset-guid-contract.ps1`은 이것을 `invalid=0`으로
통과시켜 왔다 — **게이트가 최상위 guid에서만 brace/quote를 먼저 벗겨내고 대소문자·버전을
가리지 않는 느슨한 패턴으로 검사한다**(subasset guid에만 canonical 패턴을 쓴다).
즉 게이트와 코드가 `.meta` 유효성을 서로 다르게 정의하고 있었고, 초록은 게이트 규칙
아래에서만 참이었다. 7개는 전부 untracked·gitignore 대상이며 저작 자산이 참조하지
않는다(scene/prefab/asset 전수 grep 0건). 표기 정규화는 `--refresh-model-identities`와
같은 **authoring 경계**의 별도 작업으로 둔다 — cook이 데이터를 조용히 고치면 안 된다.

★ **`.dds`는 실자산 커버리지가 0이다.** 하나뿐인 `blueNoise.dds`가 위 7개에 들어간다.
allowlist 세 갈래 중 하나가 실자산으로는 아예 안 돌므로 합성 게이트가 유일한 증거다.

게이트 `experiment.texcook`(합성 78 · 실자산 5)를 신설했다. **변이 4종으로 이빨을
확인했다** — allowlist 무력화 6건, 경로 헬퍼 검증 제거 5건, 해시 상수화 4건,
0바이트 검사 제거 3건이 각각 정확히 빨개졌다. VS18/v145 Debug x64 AssetCooker·
CreatorEditor 빌드 성공, experiment 게이트 13회 호출 전수 통과(실패 0).

**D5-c — Player manifest/catalog scene+cooked load — ⏳ 잔여.** Player가 `.meta`나 source
path 탐색 없이 manifest와 cooked bytes만으로 scene/model/material 의존성을 해석하게 하고,
그 뒤에만 `.meta`를 pak에서 제외한다. missing/duplicate/stale manifest entry는 fail-closed다.

**D5-d — 전수 cook parity + D0 성능·실플레이 — ⏳ 잔여.** scene 14·prefab 9·standalone
material 2의 authoring 결과와 cooked load 결과를 전수 비교하고, Player 씬 전환을 D0과
대조한 뒤 실제 게임 빌드 플레이로 D5 전체를 닫는다.

**D6 — Player 텍스트 경로 은퇴 (1일)**
EngineSettings·TagManager 등 부팅 YAML도 쿠킹 대상에 편입. 판정:
**Player 실행 중 text parser 호출 0**(로그 훅 계측) + Player Runtime source의
ryml include 0. yaml-cpp/nlohmann consumer 제거는 D3-b/D4에서 이미 끝나며, ryml의
물리 링크·PE import를 Editor에만 가두는 최종 판정은 EngineLayerSeparation E6와
PHASE 20 N9가 맡는다.

**D7 — 에디터 웜 캐시 (선택, 1일)**
에디터도 쿠킹 캐시를 mtime 비교로 활용해 반복 로드를 가속. D5의 부산물이
공짜로 생기는지 보고 결정 — 안 되면 하지 않는다(YAGNI).

합계 **16.5일**(D7 제외 15.5일). 순서 제약: D0 → D1·D2 병행 가능 → D3-a →
D3-b·D5 병행 가능(D5는 D2의 GUID 확정도 선행) → D4 → D6. D3-a가 전체의
이음새이고, PHASE 20 N1은 D3-a/D3-b/D4 완료 뒤 시작한다.

---

## 5. 완료 기준

1. **Player/Server 실행 중 text parser 호출 0** — ryml은 Editor authoring의 것.
2. **씬 전환 시간 D0 기준선 대비 수치 개선** — 목표치는 D0 후 확정.
3. **리네임/이동/동명 파일에서 참조 불변** — GUID 충돌 경고 0.
4. **pak에 소스 파일·`.meta` 미포함**, 매니페스트로 해석.
5. **저장 경로 수 감소**: 애니메이터 1경로 · 머테리얼 1경로(MaterialPipelinePlan
   M5의 "직렬화 단일화"와 같은 기준을 공유).
6. **쿠킹 왕복 검사 상설화** — 회귀 세트에 편입, 전 씬·전 프리팹 자동 비교.
7. 회귀 세트 · 프리팹 왕복 검사 착수 전과 동일 판정.
8. yaml-cpp·nlohmann consumer/include 0, Editor authoring backend는 ryml 하나.
9. Entity/ComponentFactory/Runtime interface에 YAML/JSON/ryml Node 타입 0.
10. `AuthoringArchive`, `CookedArchive`, PHASE 20용 `NetworkArchive` 소비 경계가
    같은 `MetaSchema` 위에서 서로의 포맷 타입 없이 컴파일된다.

## 6. 하지 않을 것

- **저작 문법을 JSON으로 교체** — 3.1. YAML 1.2가 저작의 정본이다.
- **FlatBuffers/스키마 언어 도입** — 3.2. 리플렉션이 스키마다.
- **쿠킹 산출물의 하위 호환** — 캐시다, 재쿡한다.
- **YAML/JSON Node를 network packet으로 재사용** — PHASE 20 wire는 bitstream이다.
- **PHASE 17에서 Transport/RPC/Replication 구현** — 이 문서는 Archive 이음새까지만.
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
- **NetworkFrameworkPlan (PHASE 20)**: N1은 D3-a/D3-b/D4를 선행으로 받는다.
  PHASE 17은 format-neutral schema/Archive까지만 소유하고, NetId·fixed tick·wire
  version·replication·Transport는 PHASE 20의 정본이다.
- **EngineLayerSeparationPlan**: D3-b/D6이 source/API 경계를 만들고 E6가 물리
  프로젝트와 링크 경계를 확정한다. headless Server의 최종 parser 의존 0은 PHASE 20
  N9에서 PE import와 패키지로 증명한다.

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
- **ryml view 수명 오용** — `NodeRef`를 yaml-cpp `Node`처럼 독립 값으로 장기 보관하면
  Tree 이동/파괴 뒤 dangling이 된다. 네 장기 보관 root가 문서를 소유하고, view가
  문서보다 오래 살 수 없음을 sanitizer/canary로 검증한다.
- **Archive 만능화** — Authoring의 map/이름/동적 수정 API를 Cooked/Network에도
  강제하면 문자열 키와 allocation이 runtime에 되살아난다. 공유하는 것은 field
  visitor와 scalar codec 일부뿐이며 정책/표현은 분리한다.
