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

### 1.7 2026-08-30 착수 전 재실측 — §1.1~1.6의 절반이 낡았다

D2 완료(08-29)와 다른 페이즈(PHASE 18 리플렉션 재설계, EngineLayerSeparation E 트랙)의
진행이 이 문서의 전제를 양방향으로 바꿨다. **일부는 이미 해결됐고 일부는 악화됐다.**
아래는 착수 직전 전수 재측정 결과이며, 이후 슬라이스 판정은 이 값을 기준선으로 쓴다.

| 항목 | §1.1/§1.6 기록 | 2026-08-30 실측 | 방향 |
|---|---|---|---|
| yaml-cpp 소비 (`YAML::`+`MetaYml::`) | 43파일 · 268 matches | **52파일 · 378 matches** | ▲ 악화 |
| `nlohmann::json` 소비 | 16파일 · 44 matches | 15파일 · 38 matches | ≈ |
| ryml/rapidyaml 소비 | — | **0파일** | D3-b 미착수 확인 |
| scene `.creator` | 12 → 14 | **14** (저작 코퍼스) | = |
| prefab `.prefab` | 206 → 9 | 9 | = |
| `.meta` | 598 → 226 | **240** | ▲ |
| 장기 보관 Node | 4곳 | 4곳 전부 생존 | = |

★ **자체 정정(같은 날).** 위 scene 행을 처음에 16으로 적었다. `Artifacts/` 와
`Bin/x64-Debug/` 아래 산출물 `.creator` 2건을 저작 코퍼스로 잘못 셌기 때문이다.
`Dynamic_CPP/Assets` 한정 전수는 scene 14 · prefab 9 · `.meta` 240이며 D2-d의 코퍼스
정의와 일치한다. **경로 경계 없는 전역 find는 산출물을 저작물로 오염시킨다** — 이후
모든 코퍼스 계수는 `Dynamic_CPP/Assets` 기준이다.

**정정 ⑥ — D0의 측정 대상 하나가 존재하지 않는다.** §1.1과 D0 본문이 지목한
"최대 프리팹 UI_CanvasesIngame 462KB"는 물론 "prefab 206개·3.9MB" 자체가 현재 저장소에
없다. 남은 prefab 9개는 **전부 회귀 프로브**이고 최대가
`NestedProbeParent.prefab` **3,289바이트**다. 씬은 최대 `Test1.creator` **82,985바이트**
(§1.1 기록 49KB → 증가). 따라서:

- D0는 "462KB 프리팹"을 잴 수 없다. 프리팹 항목은 **현존 최대(3.3KB)** 로 재정의한다.
- ★ **이 기준선은 실게임 부하를 대표하지 않는다.** 프로젝트 분리로 콘텐츠가 빠진
  코퍼스이므로, D0 표는 "현 저장소 코퍼스의 절대값"이자 **D5 전후 A/B의 동일 조건
  비교자**로만 쓴다. §5 완료 기준 2의 "수치 개선" 판정도 같은 코퍼스 내 상대 비교다.
  절대 부하 판정이 필요해지면 콘텐츠 복귀 후 재측정한다 — 이 한계를 표에 명시한다.

D2가 닫힌 뒤에도 저작 표면이 계속 자란다. **D3-a/D3-b는 착수가 늦을수록 단조 증가하는
비용**이며, 이 수치가 그 증거다.

**정정 ① — Y-3의 절반은 이미 죽었다.** `ScanAndGenerateMissingMeta`는 **저장소에 심볼
자체가 없다**(§1.2가 지목한 `DataSystem.cpp:93-101`은 사라졌다). efsw 워처도
`Editor/EngineEntry/EditorAssetDatabase.cpp:407` 한 곳뿐이고 `Engine/` 참조는 0이다.
`Player.vcxproj`는 `EngineEntry`를 링크하지 않으므로 **Player 프로세스에 efsw 스레드도
`.meta` 생성 스캔도 없다** — E 트랙이 먼저 밀어냈다.

**정정 ② — 그러나 Player의 부팅 전수 스캔은 살아 있고, 위치가 다르다.**
`PlayerMain.cpp:139`의 `DataSystems->Initialize()`가 `DataSystem::LoadAssetCatalog`
(`DataSystem.cpp:210`)를 부르고, 이 함수는 asset root를 **recursive_directory_iterator로
전수 순회하며 `.meta` 240개를 개별 `YAML::LoadFile`로 파싱**한다. 즉 남은 비용은 "생성"이
아니라 **catalog 구축**이고, 이것은 D1이 아니라 **D5-c 잔여(Player catalog 배선)의 대상**이다.
`Experiment/Cooked/CookedAssetCatalog.h:15`가 이미 이 함수를 대체 목표로 지목하고 있다.

**정정 ③ — Y-4(pak 필터)만 원문 그대로 유효하다.** `scriptExtensions`는
`Engine/Utility_Framework/PakHelper.h:194`에 선언되고 **저장소 전체 참조가 1회(선언)뿐,
사용 0회**다. 수집 루프는 무필터 `addFile`이라 `.h/.hpp/.cpp`가 그대로 배포물에 실린다.

**정정 ④ — D3-a의 최대 난관이 스스로 무너졌다.** §1.3과 §8이 가장 무겁게 잡았던
`ComponentFactory::LoadComponent`("680줄, 리플렉션 밖 수기 노드 접근 27곳")는 현재
**203줄 · 수기 접근 1곳**(`ComponentFactory.cpp:111`의 `itNode["ModuleBehavior"]`)이다.
PHASE 18이 흡수했다. 반면 장기 보관 Node 4곳과 `YAML::Dump` 문자열 비교
(`PrefabUtility.cpp:55,62,395`)는 그대로 남아 D3-a의 본체가 됐다.

**정정 ⑦ — D3-a의 본체는 리플렉션 계약이고, 그 파급이 §1.3 기록보다 크다.**
§1.3이 지목한 헤더 4개 중 **3개가 이미 사라졌다**(`ReflectionYamlTemplete.h`·
`ReflectionVectorInvoker.h`·`ReflectionVectorMapper.h` — PHASE 18이 재편). 현재 코어는
`ReflectionYml.h`(288행) + `ReflectionTypedYml.h`(658행) 둘이다. 그러나 **줄어든 것은
파일 수뿐이고 결합은 그대로다**: `Meta::Typed::TypeOps`의 함수 포인터 셋
(`serialize`/`deserialize`/`postLoad`)이 모두 `MetaYml::Node`를 시그니처에 갖고,
그것이 컴포넌트의 `OnDeserialized(const YAML::Node&)`로 전파된다.

착수 시점 파급 실측:

| 표면 | 수 |
|---|---:|
| `TypeOps` 함수 포인터 시그니처 | 3 |
| `OnDeserialized` 선언 / 정의 | 22 / 9 |
| `OnAfterSerialize` | 5 |
| `Meta::Serialize` / `Meta::Deserialize` 호출부 | 28 / 38 |
| YAML 타입을 노출하는 Engine 헤더 | 13 |
| `Entity.h` 4곳의 소비자 합 | 15+ |

즉 "Entity/ComponentFactory와 Runtime interface의 YAML Node 타입 0"(§5 완료 기준 9)은
**리플렉션 계약 타입 교체 없이는 달성되지 않는다.** 2일 추정은 이 파급을 반영하지
않은 값이며, 단일 슬라이스로 밀면 위험하다 — 하위 슬라이스로 분해한다(D3-a 절).

**정정 ⑧ — `PrefabOverride::m_valueYaml`은 비교용 임시값이 아니라 파일 포맷이다.
이것이 D6 완료 기준 1과 충돌한다.** ★ 이 문서 어디에도 없던 제약이다.

`m_valueYaml`은 `meta::field<&Self::m_valueYaml>`로 **직렬화되는 필드**이고, 값은
YAML을 덤프한 **문자열**이다(`PrefabUtility.cpp:63`). 되먹일 때는 그 문자열을 다시
파싱한다 — `MetaYml::Load(ov.m_valueYaml)`(`PrefabUtility.cpp:345`). 그리고 그 경로는
에디터 전용이 아니다: `Prefab.cpp:649`의 `ApplyRecordedOverrides`가 **프리팹 소환
경로**에 있으므로 Player도 탄다.

결과 둘:
- **D3-a는 이 셋을 건드릴 수 없다.** `YAML::Dump` 3곳 중 `PrefabUtility.cpp:55`만
  구조 비교로 교체 가능하고, `:62`·`:395`는 **저장 표현**이라 바꾸면 파일 포맷이
  바뀐다(§7 SceneGraphRedesignPlan §5 충돌, D2 코퍼스 게이트 전부 재베이스).
  Y-6의 "포맷 교체의 직접 장애물"은 비교가 아니라 **이 저장 표현**이었다.
- **D6 완료 기준 1("Player 실행 중 text parser 호출 0")이 현 데이터 모델로는
  성립하지 않는다.** 오버라이드를 가진 프리팹 인스턴스를 복원하려면 Player가
  YAML 문자열을 파싱해야 한다. 닫으려면 오버라이드 값 표현을 문자열에서
  타입 있는 표현으로 바꿔야 하고, 그것은 **파일 포맷 변경**이라 D5(쿠킹)나 트랙 P와
  묶어야 한다. D6 착수 전에 이 항목의 소유를 정한다.
  현재 저작 데이터의 실사용은 1건(`NestedUpdateParent.prefab`의 `m_valueYaml: false`)이라
  이행 부담 자체는 작다 — 작은 것은 데이터고, 계약이 아니다.

**정정 ⑤ — experiment 트리가 새 YAML 표면을 만들고 있다.**
`Engine/RenderEngine/Experiment/` 아래 YAML 소비 파일이 5개다(Cooked producer 3 +
`MaterialAuthoringCodec.h/.cpp`, 후자는 I5-M5-S0 산출물). `Player.vcxproj`가
`RenderEngine.vcxproj`를 링크하므로 **저작 코덱이 런타임 모듈에 상주**한다. D6의 완료
기준 1(호출 0)은 여전히 달성 가능하지만, 물리 링크 판정을 맡는 E6/N9 시점에는 이 배치
자체가 재검토 대상이다. D6 착수 시 위치 이동 여부를 결정한다.

---

## 2. 결함 목록

| # | 심각도 | 내용 | 위치 |
|---|---|---|---|
| Y-1 | HIGH | 동명 파일 GUID 충돌 + 레지스트리 무경고 덮어쓰기 — 참조가 엉뚱한 애셋으로 해석될 수 있다 | `AssetMetaWather.h:330` · `AssetMetaRegistry.h:7-11` |
| Y-2 | HIGH | 리네임 시 GUID 변경 — 참조 안정성 부재. sidecar 설계 목적 미달 | 같은 곳 |
| Y-3 | ~~HIGH~~ → ✅ D1(고정) + D5-c(잔여) | ~~Player가 `.meta` 생성 스캔 + efsw 워처를 런타임 내내 구동~~ **08-30 정정(§1.7 ①②): 생성 스캔·워처는 소멸했고 D1이 `verify-player-runtime-hygiene.ps1`로 그 상태를 고정했다.** 남은 것은 부팅 시 `.meta` 240개 전수 YAML 파싱으로 catalog를 세우는 비용(D0 실측 53.6 ms)이며 **D5-c 잔여가 소유** | ~~`DataSystem.cpp:93-101`~~ → `DataSystem.cpp:210` · `PlayerMain.cpp:139` |
| Y-4 | MEDIUM → ✅ D1 | pak 필터 죽은 코드. **08-30 정정: 지목한 함수 `PakHelper::PackageGameAssets`가 호출자 0인 죽은 코드였고, 실제 pak은 `AssetPacker.cpp`가 만든다.** 필터는 그쪽에 넣었다(D1 절 참조). `.meta`는 D5까지 존치 | ~~`PakHelper.h:164-168`~~ → `Tools/AssetPacker/AssetPacker.cpp` `IsExcludedSourceExtension` · `CollectFiles` |
| Y-5 | MEDIUM | 애니메이터 데이터 이중 직렬화(씬 YAML + 에디터 json) — 두 사본의 어긋남은 시간 문제 | `Animator.h:91` · `Animator.cpp:308,333` |
| Y-6 | MEDIUM | 프리팹 오버라이드 diff가 `YAML::Dump` 문자열 비교 — 포맷 교체의 직접 장애물이자 비용. **08-30 재확인: 유효하되 호출부가 이동** | ~~`ReflectionYml.h:348`~~ → `PrefabUtility.cpp:55,62,395` |
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

**D0 — 계측: 기준선 확정 (0.5일) — ✅ 구현·측정·게이트 편입 완료 (2026-08-30)**
직렬화 성능 실측이 0인 상태(1.1)에서 시작하지 않는다. 씬 전환 1회를
파싱(LoadFile)/역직렬화(Meta::Deserialize)/컴포넌트 특례(ComponentFactory)로
분해 계측하고, ~~최대 프리팹(UI_CanvasesIngame 462KB)~~ **현존 최대 프리팹(§1.7 ⑥)** 의
로드·Instantiate를 따로 잰다. 산출: 이 문서에 기준선 표 추가.
**모든 후속 슬라이스의 이득은 이 표 대비 수치로 판정한다.**

**계측 방식 — 제품 경로 안에서 잰다.** `Engine/Utility_Framework/SerializationProfiler.h/.cpp`
가 단계별 누적 시간·호출 수를 원자적으로 모으고, `Scope`가 제품 로드 경로 본체에 직접
들어간다(`SceneManager::LoadSceneImmediate`·`MetaYml::LoadFile` 호출부 2곳·
`Meta::Deserialize(obj,itNode)` 3곳·`ComponentFactory::LoadComponent` 본체·
`PrefabUtility::LoadPrefabFullPath` 캐시 미스 구간·`InstantiatePrefab` 오버로드 2곳·
`DataSystem::LoadAssetCatalog`). **벤치가 경로를 재현하지 않는다** — dx12.encoderbench가
모형만 재고 있던 전례를 직렬화판으로 반복하지 않기 위해서다. 대가로 계측 중에는 로드
경로에 시계 읽기가 얹히므로 기본은 꺼짐이고 CLI가 켰다가 되돌린다.

★ **PHASE 14 프로파일러를 쓰지 않은 이유.** 그 프로파일러는 프레임 링 히스토리 구조라
(`Tick`/historySize) "씬 전환 1회" 같은 비주기 일회성 구간을 문서에 옮길 결정적 수치로
뽑기에 맞지 않다. 착수 전 계획은 그것을 쓴다고 적었으나 실물을 보고 바꿨다.

CLI는 `serialize.bench boot|scene <경로> [반복]|prefab <이름> [반복]`이고, 게이트는
`Tools/regression/verify-serialization-baseline.ps1`이다(`-Baseline`으로 기준선 표 출력).

**D0 기준선 (2026-08-30 · Release x64 · VS18/v145 · 워밍업 1회 후 5회 평균 ×
프로세스 3회 실행)**

★ **단일 실행값을 기준선으로 못 박지 않는다.** 같은 exe·같은 코퍼스로 세 번 돌린
결과가 서로 흔들렸다(아래 범위 열). 한 번만 재고 표를 굳혔다면 후속 슬라이스의
"개선"이 노이즈와 구분되지 않았을 것이다. 판정에는 **평균**을 쓰고, 변동폭보다 작은
차이는 개선으로 읽지 않는다.

씬 전환 1회 분해:

| 단계 | Test1 (82,985B) 평균 | 3회 범위 | FT_Primitives (23,412B) 평균 | 3회 범위 |
|---|---:|---:|---:|---:|
| **SceneLoadTotal** | **32.239 ms** | 30.20–34.60 (±6.8%) | **12.279 ms** | 11.31–13.15 (±7.5%) |
| SceneParse | 19.351 ms (60.0%) | 17.67–21.18 | 5.651 ms (46.0%) | 5.12–6.15 |
| EntityDeserialize | 0.994 ms (3.1%) | 0.96–1.03 | 0.215 ms (1.8%) | 0.17–0.26 |
| ComponentLoad | 5.384 ms (16.7%) | 5.11–5.70 | 4.097 ms (33.4%) | 3.89–4.28 |
| 미귀속 | 6.510 ms (20.2%) | — | 2.316 ms (18.9%) | — |

회차당 호출 수: Test1 엔티티 68 · 컴포넌트 134, FT_Primitives 엔티티 11 · 컴포넌트 22.

부팅 catalog(§1.7 ②, D5-c 대체 대상): **평균 53.560 ms · `.meta` 240개 · 223 µs/개**
(범위 44.73–64.16 ms, **±18%로 이 표에서 가장 불안정** — 파일 캐시 상태에 좌우된다.
D5-c 판정 시 반드시 여러 번 재고 범위를 함께 적을 것.)

프리팹(`NestedProbeParent`, 3,289B · 10회):
정의 로드 1.41–2.82 ms(캐시 미스 1 + 중첩 정의 1) · **Instantiate 평균 2.086 ms/회**
(범위 1.92–2.36) · 그중 ComponentLoad 0.114–0.260 ms/회.

★ **이 표에서 읽어야 할 것.**
1. **파싱이 씬 로드의 최대 항목이다**(60.0% / 46.0%). D3-b(ryml)와 D5(cooked binary)의
   이득 상한이 여기 있고, 반대로 그 둘이 파싱을 0으로 만들어도 씬 전환이 0이 되지는
   않는다 — 미귀속 19~20%는 직렬화 밖이다.
2. **부팅 catalog 53.6 ms는 씬 전환 1회(12.3~32.2 ms)보다 크다.** D5-c의 우선순위 근거다.
3. **씬 크기와 비용이 비례하지 않는다.** Test1은 FT_Primitives의 3.5배 크기인데 로드는
   2.6배다. 컴포넌트당 비용이 지배적인 FT_Primitives 쪽에서 ComponentLoad 비중이 두 배
   가까이(33.4% vs 16.7%) 높다 — 파일 크기 하나로 부하를 추정하면 안 된다.
4. 코퍼스 한계는 §1.7 ⑥에 적은 그대로다 — 상대 비교자이지 실게임 절대 부하가 아니다.

★ **Debug로 재면 결론이 달라진다(실측).** 같은 워크로드의 Debug/Release 배율은
SceneParse 15.9배 · SceneLoadTotal 13.9배 · ComponentLoad 5.5배 · AssetCatalog 4.6배로
**단계마다 다르다.** Debug에서는 SceneParse 비중이 67%로 보여 파싱의 몫을 과대평가하게
된다. 그래서 게이트의 기본 exe는 Release이고, Release가 없으면 Debug로 대체하지 않고
실패한다. CLI 출력도 `config=` 로 자기가 어떤 구성인지 먼저 말한다.

**동작 불변 검증.** 계측 Scope가 제품 로드 경로 본체에 들어갔으므로 왕복 게이트로
부작용 없음을 확인했다 — scene authoring 전수(14개, load 28/28 · save 28/28 ·
unstable 0 · sourceMutations 0), prefab authoring 전수(9개, identity/override/등록
multiset 보존), material authoring 왕복(2/2), FT_Primitives 실제 DX12 draw(draws=8).
D0 게이트 자체는 `selfchecks=4 pass=4 · unexpectedStderr=0`으로 통과했다.
VS18/v145 x64 Debug·Release 빌드 모두 exit 0.

★ **게이트가 자기 출력 형식에 걸려 한 번 빨개졌다.** boot 모드의 `selfcheck=`를
`perMetaUs=`와 같은 줄에 찍었더니 게이트의 `mode=X selfcheck=Y` 정규식이 그 한 줄만
놓쳐 "selfcheck 3개(4개 기대)"로 실패했다. 수치는 멀쩡했고 계약 검사가 계약 표기를
못 읽은 것이다. **출력 형식은 장식이 아니라 계약**이므로 `selfcheck`를 항상 독립 라인으로
분리했다. 게이트가 "덜 돈 검사"를 통과로 읽지 않게 라인 수를 세고 있었기에 잡힌 것이다.

★ **게이트의 이빨을 변이로 증명했다.** `ComponentFactory`의 계측 Scope를 제거한 변이본을
빌드해 돌렸더니 scene 모드는 `selfcheck=fail reason=child-stage-zero-calls`로 정확히
빨개졌으나 **prefab 모드는 그대로 통과했다** — prefab 자가 검증에 ComponentLoad 0 검사가
빠져 있던 실제 구멍이다. `component-load-zero-calls` 분기를 추가해 변이본에서 fail을
확인한 뒤 원본으로 복구했다. 변이로 증명한 것은 zero-calls 계열 분기이며,
`child-sum-exceeds-root`는 논리만 있고 아직 변이로 밟지 않았다 — 그 사실을 숨기지 않는다.

**D1 — 런타임 위생 (Y-3·Y-4) — ✅ 구현·표적 검증·게이트 편입 완료 (2026-08-30)**
~~`ScanAndGenerateMissingMeta`를 "등록 전용 스캔"과 "생성"으로 분리하고 Player는
전자만 수행. efsw 워처는 에디터 전용으로 가드.~~ **08-30 정정(§1.7 ①): 이 두 항목은
다른 트랙이 이미 닫았다 — 심볼 부재·efsw는 Editor 전용 프로젝트에만 존재하며 Player는
`EngineEntry`를 링크하지 않는다. 착수 시 "이미 참"임을 게이트로 고정하기만 한다.**
★ `.meta`는 D5 매니페스트 전까지 Player의 GUID 해석에 필요하므로 **제외하지 않는다.**
**부팅 `.meta` 전수 파싱 제거는 D1이 아니라 D5-c 잔여의 몫이다(§1.7 ②).**

★ **착수 실측이 Y-4의 대상을 통째로 바꿨다 — 세 가지가 계획과 달랐다.**

1. **필터를 고칠 함수가 죽어 있었다.** `PakHelper::PackageGameAssets()`는 저장소 전체에
   **호출자가 0**이다. 그 안의 `scriptExtensions`는 죽은 코드 안의 죽은 코드였고,
   고쳐 봐야 아무 배포물도 바뀌지 않는다. `PakHelper.h`를 include하는 유일한 파일인
   `Player/PlayerApp.cpp`는 **언팩 쪽만** 쓴다.
2. **실제 pak은 별도 도구가 만든다.** `Tools/AssetPacker/AssetPacker.cpp`가 `Pak::Builder`를
   직접 쓰며, 그 `CollectFiles`에는 **확장자 필터가 전혀 없었다.** Y-4가 적용되어야 할
   지점은 여기다. 이 슬라이스는 그 필터를 새로 넣었고 `PakHelper`는 건드리지 않았다 —
   ★ **소비자 0인 `PackageGameAssets()` 제거는 BuildPipelinePlan E2가 이미 소유를
   선언했다**(그 문서 잔여 항목). 남의 슬라이스를 침범하지 않는다.
3. **걸러낼 대상이 현재 0건이다.** pak이 수집하는 두 루트(`Dynamic_CPP/Assets`,
   `Dynamic_CPP/ProjectSetting`)에 `.cpp/.h/.hpp`가 **0개**다(C++ 스크립트 은퇴 이후).
   C# 스크립트도 `GameScripts/`·`ScriptCore/`에 있어 수집 루트 밖이다. 따라서 이 필터는
   **현재 결함의 수정이 아니라 회귀 방지 장치**이며, 그 사실을 숨기지 않는다.

★ **과잉이 누락보다 위험한 자리다.** `.hlsl`/`.hlsli`는 제외 목록에 **넣으면 안 된다** —
현재 패키지는 셰이더 **소스**와 Slang/DXC DLL을 싣고 Player가 런타임에 컴파일한다
(BuildPipelinePlan B3). 셰이더를 "소스 파일"로 묶어 빼면 게임이 아무것도 그리지 못한다.
게이트는 배제와 보존을 **양쪽 다** 단정한다.

**게이트 둘.** `verify-pak-source-exclusion.ps1`은 clean/dirty 합성 트리를 만들어
AssetPacker를 두 번 돌린다(실자산으로 재면 "0개를 걸렀다"가 나와 아무것도 증명하지
못한다). `verify-player-runtime-hygiene.ps1`은 Engine 트리 efsw 참조 0 ·
`ScanAndGenerateMissingMeta` 심볼 0 · Player.vcxproj의 에디터 `ProjectReference` 0 ·
Player.exe 바이너리의 efsw 문자열 0을 본다. 둘 다 `run-all.ps1`에 편입했다.

★ **pak은 결정적이지 않다(실측).** 같은 입력을 두 번 패킹해도 SHA-256이 다르다.
처음에는 clean/dirty pak의 hash 동일성을 단정했다가 필터가 아니라 **내 전제 때문에**
게이트가 빨개졌다. 자를 먼저 검증하지 않은 것이다. 대신 AssetPacker에 `--list-entries`를
추가해 **reopen된 pak의 실제 entry 목록**을 대조한다 — entry 수만 세면 "빠져야 할 것이
들어오고 실려야 할 것이 빠진" 같은 수의 pak을 통과시킨다.

★ **게이트의 이빨을 변이 셋으로 증명했다.** ① 필터를 무력화하니 소스 8건(두 루트 ×
4파일, 대문자 `.CPP` 포함)이 전부 검출됐다. ② `.hlsl`을 제외 목록에 넣는 **과잉 변이**는
`실려야 할 파일이 빠졌다: Assets/Shaders/Probe.hlsl`로 잡혔다. ③ 위생 게이트의 대조군을
Player.exe로 바꾸자 `이 검사는 판별력이 없다`로 스스로 실패했다.

★ **게이트가 만들어지는 중에 자기 결함 둘을 드러냈다.** (a) Player.vcxproj를 **파일 전체
부분 문자열**로 검사해 `AdditionalIncludeDirectories`의 `Editor\EngineEntry\`를 "링크한다"로
오판하는 거짓 실패를 냈다 — `ProjectReference`의 Include 값만 읽도록 한정했다.
(b) 상대 경로 인자를 주면 경로 접두 절단에서 크래시했다(변이 실험이 아니었으면
못 봤다). 참고로 include 경로에 에디터 헤더가 열려 있는 것 자체는 별개 사안이며
EngineLayerSeparation E 트랙 소유다 — D1은 링크 그래프만 판정한다.

**판정 결과:** planted 8 / leaked 0 · clean·dirty entry 6=6 · 목록 동일 · `.hlsl`·`.hlsli`·
`.meta` 보존 · engineEfswRefs 0 · scanSymbolRefs 0 · Player.exe efsw 0(대조군 Editor.exe는
검출) · 기존 `verify-asset-packer-boundaries` 회귀 없음. AssetPacker Release 빌드 exit 0.

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

**D3-a — format-neutral Document/Archive 경계 (Y-6, ~~3일~~ → 2일)**
장기 보관 4곳 + SceneManager 시그니처 + ComponentFactory 수기 접근을 3.3의
문서/Archive 경계 뒤로 옮긴다. 이 슬라이스는 yaml-cpp backend를 유지하는 동작 불변
리팩터다. Dump 문자열 diff → 구조 비교, `ComponentFactory::LoadComponent` →
정규화된 `Meta::Type` 생성/적용 경계로 바꾼다. 판정: Entity/ComponentFactory와
Runtime interface의 YAML Node 타입 0 + 회귀·왕복 검사 통과 + D0 대비 비퇴행.
`ISerializable`의 JSON 고정 계약은 D4가 닫는다.

★ **08-30 정정(§1.7 ④): 부담 재산정.** `ComponentFactory`는 680줄·수기 27곳이 아니라
**203줄·수기 1곳**(`itNode["ModuleBehavior"]`)이므로 이 슬라이스의 최대 항목이 아니다.
본체는 남은 셋이다 — ① 장기 보관 Node 4곳(`Entity.h:381`·`Prefab.h:73`·
`SceneManager.h:216`·`GameObjectCommand.h:106`), ② `YAML::Dump` 문자열 비교
3곳(`PrefabUtility.cpp:55,62,395`), ③ **저작 표면 자체의 증가**(52파일·378 matches,
§1.7 표). ③ 때문에 지연 비용이 단조 증가하므로 D0 직후 착수가 최선이다.

★★ **08-30 착수 실측 — 위 재산정도 아직 작게 잡았다(§1.7 ⑦⑧).** 진짜 본체는
**리플렉션 계약**(`TypeOps`의 함수 포인터 셋)이며, 그것을 바꾸지 않으면 컴포넌트
헤더의 YAML 노출이 남아 §5 완료 기준 9가 닫히지 않는다. 그리고 위 ②의 셋 중
**둘은 저장 표현이라 D3-a가 건드릴 수 없다**(§1.7 ⑧). 그래서 이 슬라이스를 아래로
분해한다. 각 조각은 이 저장소의 규칙대로 **소비자가 먼저 있고 판정은 수치나
계약**이며, 앞 조각이 다음 조각의 발판이 된다.

| 조각 | 내용 | 규모 | 선행 |
|---|---|---|---|
| **D3-a-1** ✅ | 오버라이드 시딩의 `Dump` **비교**(`PrefabUtility.cpp:55`)를 구조 비교로. 저장 표현(`:62`)은 그대로 둔다 | 소 | — |
| **D3-a-2** ✅ | `Entity.h`의 정적 읽기 진입 **2종**(`InferCreationType`·`ReadSerializedHierarchy`)을 저작 읽기 어댑터로 분리. 소비자 9 | 중 | D3-a-1 |
| **D3-a-3a** ✅ | `Authoring::Document` 도입 + `SceneManager::m_editorSceneBackup` 적용(소비자 5, 전부 내부) | 중 | D3-a-2 |
| **D3-a-3b** ✅ | `Entity::m_prefabOriginal` 적용(소비자 6) | 소 | D3-a-3a |
| **D3-a-3c** ✅ | `Prefab::m_prefabData` 적용. 접근자를 `.cpp`로 내려 소비자 20여 곳은 수정 0 | 중 | D3-a-3b |
| **D3-a-4** ✅ | 컴포넌트 훅의 노드 인자를 `Authoring::NodeView`로. **실제 대상 4곳**(아래 정정) | 중 | D3-a-3 |
| **D3-a-5a** ✅ | `ComponentFactory.h`·`SceneManager.h`·`Scene.h` — 완료 기준 9가 **명시한 이름**을 닫는다 | 중 | D3-a-4 |
| **D3-a-5b** ◐ | 인자형 완료(`DataSystem::DeserializeMaterialPayload`·`Prefab::SetPrefabData`). **반환형은 D3-b로**, `MaterialAuthoringCodec`은 I5-M5 소유, `BTBuildGraph.h`는 CP949 | 중 | D3-a-5a |
| **D3-b-CRLF** ✅ | 저작 개행 LF 고정 — 자산 243개 변환 + writer 11곳 binary + `.gitattributes` 명시 | 소 | — |
| **D3-b-0** ✅ | ryml 도입 + 파서 동등성 프로브(Release 12.70×, 구조 diff 0) | 중 | — |
| **D3-b-1** ✅ | ryml 에러를 abort→예외로. **제품 경로 투입의 선결 조건** — 변이 2회로 게이트 이빨 증명 | 소 | D3-b-0 |
| **D3-b-2a** ✅ | 쓰기 뷰를 `MutableNodeView`로 분리(대상 3곳) — 읽기만 옮길 수 있게 한다. 변이로 훅 발화 증명 | 소 | D3-b-1 |
| **D3-b-2b-0** ✅ | 스칼라 **변환** 파리티 — 44케이스 중 **11건 갈림**. 알려진 목록 + 코퍼스 위험 표기 고정 | 중 | D3-b-2a |
| **D3-b-2b-1a** ✅ | 스칼라 변환을 문자열 위로 이식(`Authoring::Scalar`) — 67케이스 차이 0, 이식 오류 3건을 게이트가 잡음 | 중 | D3-b-2b-0 |
| **D3-b-2b-1b-1** ✅ | 읽기 어댑터 `Authoring::ReadNode` 도입 + 타입 디시리얼라이저 이관. 행동 변화 0, 탈출구 6곳 | 중 | D3-b-2b-1a |
| **D3-b-2b-1b-2a** ✅ | 맵 순회 추가 + `ExtractTypeFromYAML` 이관. 변이로 분기 생존 확인 | 중 | D3-b-2b-1b-1 |
| **D3-b-2b-1b-2b** ✅ | 훅 인자를 어댑터로(`NodeViewAccess::Node` 반환형) + 래칫 게이트. 탈출구 15/14 기준선 | 중 | D3-b-2b-1b-2a |
| **D3-b-2b-1b-2c** ◐ | 씬 읽기 경로 소비자 이관 — 래칫 15→12. 잔여는 backend 교체와 함께 사라진다 | 중 | D3-b-2b-1b-2b |
| **D3-b-2b-1b-3a** ✅ | 어댑터를 이중 backend로 + 어댑터 파리티 게이트(278파일·15,339노드 차이 0) | 대 | D3-b-2b-1b-2c |
| **D3-b-2b-1b-3b** ◐ | 훅 이관 + 뷰를 두 backend로(불투명 2워드+태그). 래칫 12→10 | 중 | D3-b-2b-1b-3a |
| **D3-b-2b-1b-3c** ☐ | 씬 `LoadFile` → `parse_in_arena`. **선행: experiment 재질 코덱이 어댑터를 받아야 한다(I5)** | 중 | D3-b-2b-1b-3b |
| **D3-b-L** ◐ | leaf 파서를 ryml로(BlackBoard·TagManager·ShaderMeta 완료, ~35곳 잔여). **계획서에 없던 슬라이스** | 중 | D3-b-1 |
| **D3-b-3** ☐ | 쓰기 경로(`Meta::Serialize`·Emitter). 파싱 이득 없음 — 저작 왕복 정확성이 판정 | 중 | D3-b-2b-1b-3c |
| **D3-b-4** ☐ | `Document::Impl` 교체 + Access 반환형. **첫 단계가 아니라 마지막이다**(아래 정정) | 중 | D3-b-3 |

**D3-a-1 — 구조 비교 — ✅ 구현·표적 검증·게이트 편입 완료 (2026-08-30).**
`Engine/Utility_Framework/AuthoringNodeEquality.h/.cpp` 신설 — `Authoring::NodesEqual`이
정본이다. `PrefabUtility.cpp`의 시딩 비교 한 곳이 소비자이고, 나머지 두 곳(`:62`·`:395`)은
**저장 표현이라 손대지 않았다**(§1.7 ⑧). 이 파일은 D3-b 이후 `AuthoringArchive`의 구조
비교로 흡수된다.

★ **동작을 그대로 옮기지 않았다 — 그것이 목적이다.** 판정 규칙은 스칼라=문자열 동등,
시퀀스=길이+**순서** 동등, 맵=키 집합+값 동등(**순서 무시**)이다. Dump 비교는 표기에
민감해서 맵 키 순서나 flow/block 스타일만 달라도 "다름"을 냈고, 시딩에서 그것은
**바꾼 적 없는 프로퍼티를 오버라이드로 기록하는 거짓 양성**이었다. 문자열 두 개를
할당하지 않는 것은 부수적 이득이지 목적이 아니다.

★ **자가 검사가 구현 결함을 즉시 잡았다.** 첫 실행에서 `map-key-order`·`map-style`·
`nested-key-order` 세 건이 빨갰다 — 맵 키 순서 무시가 **통째로 동작하지 않았다.**
원인은 `rhs[entry.first]`였다: yaml-cpp에서 Node를 키로 인덱싱하면 값이 아니라 **노드
identity**로 찾으므로, 같은 문자열 키라도 다른 문서에서 온 노드는 못 찾는다. 스칼라 키를
`Scalar()` 문자열로 조회하도록 고쳐 14/14가 됐다. 이 세 케이스가 없었다면 "구조 비교로
바꿨다"고 적어 놓고 실제로는 **Dump보다 나쁜** 비교(맵을 항상 다르다고 판정)를 넣을 뻔했다.

**게이트는 갈림 자체를 단정한다.** `verify-authoring-node-equality.ps1`은 14개 판정 규칙과
함께 **`divergedFromDump=0`이면 실패**로 본다 — 전부 Dump와 같은 답이면 이 슬라이스가
아무것도 바꾸지 않았다는 뜻이고, 그때 통과를 내주면 빈 집합을 성공으로 읽는 것이다.
현재 갈림은 의도한 3건이다. 변이(시퀀스 원소 비교 제거)로 `seq-order-matters`가 정확히
빨개지는 것을 확인하고 복구했다.

**회귀:** prefab authoring 전수(9/9, override 1건 보존) · prefab nested update ·
play round-trip · scene authoring 전수(14, 28/28) 모두 통과. Debug 빌드 exit 0.

**D3-a-2 — 저작 읽기 어댑터 분리 — ✅ 구현·검증 완료 (2026-08-30).**
`Engine/SceneRuntime/EntityAuthoringRead.h/.cpp` 신설 —
`EntityAuthoring::InferCreationType`·`ReadSerializedHierarchy`가 정본이다. 소비자 9곳
(`Object.cpp`·`Prefab.cpp`·`SceneManager.cpp` 3쌍·`GameObjectCommand.h`)을 모두 옮겼고
`Entity::` 잔존은 0이다. `Entity.h`의 YAML 노출은 **4곳 → 2곳**으로 줄었다.

★ **불투명 view 타입을 만들지 않았다.** §3.3의 `AuthoringNodeView`는 **장기 보관 root**를
위한 장치이고, 이 둘은 단기 읽기다. 게다가 yaml-cpp `Node`는 값 의미론에 `operator[]`가
임시를 돌려주므로 `const Node*`를 보관하는 view는 dangling을 부른다. 대신 **해석 책임을
옮겼다** — 함수를 직렬화 어댑터로 빼면 `Entity.h`에서 노드 타입이 사라지고, D3-b가
backend를 바꿀 때 고칠 자리도 한 파일로 좁아진다. view 타입은 D3-a-3(장기 보관)에서
실제로 필요할 때 만든다(YAGNI).

★ **반환 타입은 옮기지 않았다.** `GameObjectType`과 `Entity::SerializedHierarchy`는
포맷과 무관한 DTO다. 이 슬라이스가 옮긴 것은 **데이터 모델이 아니라 해석 책임**이며,
그 구분을 흐리면 Entity가 자기 생성 파라미터 타입을 남에게 넘기게 된다.

★ **`OnAfterSerialize`는 옮길 수 없었다 — 착수 계획의 3종이 2종이 된 이유.**
리플렉션이 `requires { value.OnAfterSerialize(node); }` 형태로 **멤버 함수의 존재를
탐지해** 호출한다(`ReflectionTypedYml.h:519-521`). 자유 함수로 빼면 **컴파일은 통과하고
계층 직렬화만 조용히 사라진다** — 타입 검사가 잡아 주지 않는 종류의 파손이다.
이 훅은 리플렉션 계약과 함께 D3-a-4에서 바뀐다. `Entity.h`에 그 이유를 주석으로 남겼다.

**검증:** 리플렉션 골든 diff 0(직렬화 77 타입). ~~`OnAfterSerialize` 훅이 살아 있음을
이 게이트가 증명한다(끊겼다면 계층 블록이 골든에서 사라진다).~~ ★ **이 주장은 틀렸다 —
D3-a-4의 변이 실험이 반증했다(그 절 참조).** 골든은 등록 전 타입을 **기본 생성**해
직렬화하는데, 그렇게 만든 Entity는 `m_ownerScene`가 없어 `OnAfterSerialize`가 첫 줄에서
곧바로 return한다. 즉 골든은 이 훅을 **애초에 태우지 않는다.** scene authoring 전수
14(28/28) · prefab authoring 전수 9/9(override 1 보존) · prefab nested update ·
play round-trip · play selection/undo · D3-a-1 구조 비교 모두 통과.
Debug x64 CreatorEditor·**Player** 빌드 exit 0, Release CreatorEditor 빌드 exit 0.

**D0 대비 성능(Release, 프로세스 3회):** 판정 조건은 비퇴행이고, 결과는 비퇴행을 넘어선다.

| 항목 | D0 평균 (범위) | D3-a-1·2 이후 3회 평균 | 차이 |
|---|---:|---:|---:|
| Test1 `SceneLoadTotal` | 32.239 (30.20–34.60) | 29.871 | −7.3% |
| FT_Primitives `SceneLoadTotal` | 12.279 (11.31–13.15) | 10.777 | −12.2% |
| Prefab `Instantiate` | 2.086 (1.92–2.36) | **1.439** | **−31.0%** |

Prefab `Instantiate`는 3회 값이 1.430·1.439·1.448ms로 편차 ±0.6%이고 **D0 범위와 전혀
겹치지 않는다** — 노이즈가 아니다.

★ **다만 인과는 단정하지 않는다.** D3-a-1의 구조 비교(문자열 덤프 2회 제거)가 유력하지만,
이 구간에는 D3-a-2의 함수 이동에 따른 인라인·코드 배치 변화와 **빌드 방식 차이**(D0
측정은 클린 빌드 직후, 지금은 증분)가 함께 섞여 있다. Release LTCG에서 이 셋은 서로
구분되지 않는다. 원인을 확정하려면 D3-a-1만 되돌린 A/B가 필요하며, 이 슬라이스의
판정에는 필요하지 않으므로 하지 않았다 — **개선은 관찰로 기록하고 판정은 비퇴행으로 한다.**

**D3-a-3a·3b — 문서 소유 타입 도입 — ✅ 구현·검증 완료 (2026-08-30).**
`Engine/Utility_Framework/AuthoringDocument.h/.cpp`(+ `AuthoringDocumentAccess.h`) 신설.
`Authoring::Document`는 **pimpl로 backend를 숨기고 move-only**이며, 실제 노드 접근은
`DocumentAccess`를 통해서만 얻는다. 적용: `SceneManager::m_editorSceneBackup`(소비자 5) ·
`Entity::m_prefabOriginal`(소비자 6).

★ **소비 정책을 헤더가 아니라 include 경계로 표현한다.** 소유자의 헤더
(`SceneManager.h`·`Entity.h`)는 `AuthoringDocument.h`만 보므로 포맷 타입을 모른다.
`AuthoringDocumentAccess.h`를 include하는 TU가 곧 "직렬화 계층"이다 — 경계가 주석이
아니라 컴파일 단위로 강제된다.

★ **move-only인 이유는 지금이 아니라 D3-b 때문이다.** 현재 backend인 yaml-cpp `Node`는
값 의미론이라 복사해도 티가 안 난다. 그러나 ryml의 `Tree`는 실제로 무겁고 `NodeRef`는
트리를 소유하지 않으므로, 복사 의미론이 남아 있으면 그때 dangling view가 된다
(§8 "ryml view 수명 오용"). 문제가 드러날 때 고치는 대신 지금 타입으로 막아 둔다.

★ **새 게이트를 만들지 않았다 — 기존 게이트가 이미 이 계약의 소비자다.**
`Document::IsEmpty()`가 잘못 답하면 `HasSceneSnapshot()`이 틀리고 **play round-trip이
깨진다**. `SeedOverridesFromSnapshot`의 early return이 틀리면 **prefab override-write가
깨진다**. 즉 이 슬라이스의 판정은 이미 존재하는 두 게이트가 한다 — 여기에 별도 자가
검사를 더하면 같은 것을 두 번 재는 일이다.

**검증:** prefab authoring 전수 9/9(override 1 보존) · **prefab override-write**("로컬
수정이 기록되고 프리팹 갱신을 살아남는다") · prefab nested update · scene authoring 전수
14(28/28) · 리플렉션 골든 diff 0 · **play round-trip**(스냅샷 capture/restore 경로) ·
play selection/undo · lifecycle baseline(221 사건 순서 동일) 통과.
Debug x64 CreatorEditor·Player 빌드 exit 0.

**남은 헤더 노출:** `Entity.h` 1곳(`OnAfterSerialize` → D3-a-4) ·
`SceneManager.h` 3곳(`DesirealizeGameObject` 시그니처 → D3-a-4) · `Prefab.h` 7곳(D3-a-3c).

★ **`GameObjectCommand::m_serializedNode`는 이 슬라이스에서 뺐다.** §1.6이 장기 보관
4곳으로 세었지만 그것은 **Editor 파일**이고, §5 완료 기준 9의 대상은
"Entity/ComponentFactory/**Runtime** interface"다. 게다가 그 클래스는 헤더에 구현이
전부 인라인이라 문서 타입으로 감싸려면 `.cpp` 분리가 선행돼야 한다 — 완료 기준과
무관한 작업을 이 슬라이스에 끌어들이지 않는다. 저장소 규율(장기 보관 root는 문서가
소유)은 D3-b에서 backend를 바꿀 때 함께 적용한다.

**D3-a-3c — `Prefab::m_prefabData` — ✅ 구현·검증 완료 (2026-08-30).**
장기 보관 root 셋 중 마지막. 필드를 `Authoring::Document`로 바꾸고 `GetPrefabData`/
`SetPrefabData` 정의를 `.cpp`로 내렸다 — 헤더에 인라인으로 두면 `Prefab.h`가
`AuthoringDocumentAccess.h`를 include해야 하고, 그러면 소유자 헤더가 다시 포맷을 안다.
소비자 20여 곳은 반환 타입이 그대로라 **수정 0**이다.

`Instantiate` 두 오버로드의 `Clone(m_prefabData)`는 `Clone(definition)`으로 바뀌었고,
"소환은 정의 사본 위에서 진행한다"는 이유(`UpgradeLegacyNavigation`이 노드를 고쳐 쓴다)를
주석으로 명시했다. **`Prefab.h`의 남은 YAML 6곳은 전부 시그니처**이며(getter/setter
반환·인자 + private 정적 4종) D3-a-5의 몫이다.

★★ **`verify-prefab-duplicate`가 한 번 실패했고, 원인을 밝히지 못했다 — 그대로 적는다.**
프리팹 게이트를 연속 실행하던 중 "원본 인스턴스조차 갱신을 못 받았다"로 빨개졌다.
D3-a-1의 구조 비교를 의심해 다음을 확인했다:

1. 비교를 `YAML::Dump`로 되돌리니 통과 — 여기까지는 구조 비교가 범인으로 보였다.
2. 그래서 **추측으로 고치지 않고** 두 비교가 갈리는 지점을 로그로 찍는 프로브를 넣었다.
   같은 시나리오에서 **갈림 0건**이었다.
3. 구조 비교를 되살려 단독 3회 실행 — **3회 모두 통과**.
4. 처음 실패했던 것과 **같은 게이트 순서**로 다시 실행 — 4개 모두 통과.

즉 **구조 비교는 무죄**다(갈림 0건 + 재현 실패).

★ **원인 확정 (같은 날, 후속 조사).** 당시 나는 "정황상 게이트가 저작 프리팹을 수정하고
자기 산출물을 지운 뒤 재생성하므로 이전 세션이 남긴 초기 상태에 의존할 여지가 있다"고
적었다. **그 추측은 틀렸다.** 실제 원인은 초기 상태가 아니라 **efsw 워처**였다:

원자적 게시(`.tmp` → replace)를 목적지 경로의 Delete로 오독한 `HandleDeleted`가, 본문이
멀쩡한데도 catalog 항목과 sidecar를 떨어뜨렸다(정상 실행 한 판에 **두 번**, 각 ~26ms
실측). 그 창에 `prefab.update`가 걸리면 `LoadPrefab`이 살아 있는 identity를 널로 덮고 →
`SavePrefab`이 새 GUID를 발급하고 → `UpdateInstances`가 그 키로 조회해 **조용히 0건
적용**한다. 에러도 로그도 없고 판정 1~4는 전부 통과하므로, 우연에 맡기면 원인을 못
가른다. `HandleDeleted`는 sidecar만 사라졌을 때 본문이 살아 있으면 identity를 버리지
않도록 고쳤고, `verify-prefab-identity-injection.ps1`이 그 창을 열어 놓고 sidecar를
**밖에서 확정적으로** 떨어뜨려 재현한다(고치기 전 RED, 고친 뒤 GREEN 확인 후 편입).

★ **교훈 둘.**
1. **"1번에서 멈추지 않은 것"이 옳았다.** Dump로 되돌려 통과한 순간 "구조 비교가 회귀를
   냈다"고 적고 되돌렸다면, **무죄인 코드를 지우고 진짜 원인은 그대로 남았을 것이다.**
   A/B 한 번의 통과는 인과의 증명이 아니다.
2. **그러나 "재현되지 않으니 정황상 X"라고 적은 것도 틀린 방식이었다.** 배제한 것
   (구조 비교)은 증거가 있었지만, 남긴 추측(초기 상태 의존)은 증거가 없었다.
   **모르는 것은 "모른다"로 두고 재현 조건을 찾는 쪽에 힘을 쓰는 편이 옳다** — 실제로
   그렇게 접근한 후속 조사가 26ms짜리 경합 창을 찾아냈다.

★★ **D3-a-4 착수 실측 — "22곳"은 과대 계상이었다(2026-08-30).** §1.7 ⑦은
`OnDeserialized` 선언 22건을 파급으로 셌지만, 그 대부분은 **노드를 받지 않는다.**
저장소에 선언된 `OnDeserialized` 중 **인자 없는 형태가 13건**이고(`CameraComponent`·
`Canvas`·`FoliageComponent`·`ImageComponent`·`LightComponent`·`SpriteRenderer`·
`SpriteSheetComponent`·`Terrain`·`TextComponent` 등), 이들은 애초에 YAML을 모른다 —
리플렉션이 `requires { obj.OnDeserialized(); }`로 인자 없는 오버로드도 받아 주기 때문이다.

Runtime 헤더에서 **노드를 실제로 받는 표면은 다음 4곳**뿐이다:

| 위치 | 시그니처 |
|---|---|
| `Animator.h:87` | `OnDeserialized(const YAML::Node&)` |
| `MeshRenderer.h:37` | `OnDeserialized(const YAML::Node&)` |
| `Entity.h:101` | `OnAfterSerialize(YAML::Node&)` |
| `UIComponent.h:50` | `LoadLegacyNavigation(const YAML::Node&)` |

나머지 Runtime 헤더의 노드 시그니처는 직렬화 계층 자신의 것이거나(`EntityAuthoringRead.h`·
`Prefab.h`·`BTBuildGraph.h`·`MaterialAuthoringCodec.h`·`DataSystem.h`) D3-a-5의 몫이다.

★ **`TypeOps` 자체는 D3-a-4의 대상이 아니다.** `ReflectionYml.h`는 Utility_Framework의
**직렬화 계층**이고, 완료 기준 8이 "Editor authoring backend는 ryml 하나"라고 적은 대로
그 계층은 backend를 알아도 된다. 완료 기준 9가 지목하는 것은
"Entity/ComponentFactory/**Runtime** interface"다. `TypeOps`의 노드 타입은 D3-b가
backend를 바꿀 때 함께 바뀐다 — 여기서 미리 추상화하면 D3-b가 두 번 고치게 된다.

**D3-a-4 — 훅 인자를 `NodeView`로 — ✅ 구현·변이 검증 완료 (2026-08-30).**
`Engine/Utility_Framework/AuthoringNodeView.h`(+ `AuthoringNodeViewAccess.h`) 신설.
`Document`가 **소유**를 감췄다면 `NodeView`는 **훅 인자**를 감춘다. 이행한 4곳:

| 위치 | 이전 | 이후 |
|---|---|---|
| `Animator.h` | `OnDeserialized(const YAML::Node&)` | `OnDeserialized(const Authoring::NodeView&)` |
| `MeshRenderer.h` | 같음 | 같음 |
| `UIComponent.h` | `LoadLegacyNavigation(const YAML::Node&)` | `(const Authoring::NodeView&)` |
| `Entity.h` | `OnAfterSerialize(YAML::Node&)` | `(const Authoring::NodeView&)` |

리플렉션 썽크(`PostLoadThunk`·`HasPostLoad`·직렬화 훅)에 **뷰 오버로드를 먼저 보는 분기**를
얹었다. 기존의 backend 노드 훅과 인자 없는 훅 분기는 그대로 남아, 아직 옮기지 않은 타입도
계속 동작한다 — 인자 없는 `OnDeserialized()` 13건은 애초에 대상이 아니다.

★ **`NodeView`는 pimpl이 아니다.** 훅은 컴포넌트마다 매 로드마다 불리므로 호출당 힙
할당이 빈도에 맞지 않는다. 대신 불투명 `const void*` 하나를 들고, 생성자를 private으로
막고 `NodeViewAccess`만 friend로 둔다 — 임의 캐스팅이 성립하지 않으면서 할당은 0이다.
`Node()`는 무효한 뷰에 대해 **정의되지 않은 노드**를 돌려주므로, 기존 훅의
`if (!node)` 형태를 그대로 유지할 수 있다.

★★ **변이 실험이 내 앞선 주장을 반증했다 — 이것이 이 슬라이스의 가장 중요한 결과다.**
D3-a-2에서 나는 "리플렉션 골든 diff 0이 `OnAfterSerialize` 훅의 생존을 증명한다"고 적었다.
확인을 위해 썽크의 뷰 분기를 제거한 변이본을 빌드해 골든을 돌렸더니 — **통과했다.**
골든은 등록 전 타입을 **기본 생성**해 직렬화하고, 그렇게 만든 Entity는 `m_ownerScene`가
없어 `OnAfterSerialize`가 첫 줄에서 return한다. **골든은 이 훅을 한 번도 태우지 않는다.**

같은 변이본으로 계층 관련 게이트를 훑어 **실제로 지키는 셋**을 찾았다:

| 게이트 | 변이 시 증상 |
|---|---|
| `verify-scene-authoring-corpus` | 재로드 실패 — loads **14/28** |
| `verify-play-roundtrip` | `stop did not restore the object count: edit=3 restored=1` |
| `verify-prefab-nested` | `Prefab instance rootref가 복제 루트를 가리키지 않는다` |

★ **교훈: "게이트가 통과한다"와 "그 게이트가 이것을 지킨다"는 다른 명제다.** 훅이
끊기면 컴파일은 통과하고 동작만 조용히 사라지는 구간이라 특히 위험했다. 변이를 돌리지
않았다면 **틀린 안전 근거를 계획서에 남긴 채** 다음 슬라이스로 넘어갔을 것이다.

**검증(복구 후):** scene authoring 전수 14(28/28) · play round-trip · prefab nested ·
prefab authoring 전수 9/9 · prefab duplicate · UI navigation(구파일 승격 포함) ·
리플렉션 골든 diff 0 — 전부 통과. Debug x64 CreatorEditor·Player 빌드 exit 0.
자가 검사(`ExperimentMaterialMigrateSelfTest`)의 직접 호출 2곳도 같은 창구로 옮겼다.

**D3-a-5a — 완료 기준 9의 명시 이름을 닫다 — ✅ 구현·검증 완료 (2026-08-30).**
`ComponentFactory::LoadComponent` · `SceneManager::DesirealizeGameObject` 3종 ·
`Scene::SerializeEntityHierarchy`의 노드 인자를 `Authoring::NodeView`로 옮겼다.
호출부는 `LoadComponent` 7곳(`Object`·`Prefab`·`PrefabUtility`·`SceneManager` 3 ·
`GameObjectCommand`) + `DesirealizeGameObject` 계열 9곳이다.

**완료 기준 9가 지목한 표면의 현재 값:**

| 헤더 | 착수 시 | 현재 |
|---|---:|---:|
| `Entity.h` | 4 | **0** |
| `ComponentFactory.h` | 1 | **0** |
| `SceneManager.h` | 4 | **0** |
| `Scene.h` | 1 | **0** |
| `Animator.h` · `MeshRenderer.h` · `UIComponent.h` | 각 1 | **각 0** |

★ **그러나 완료 기준 9는 아직 닫히지 않았다.** 문구가 명시한 두 이름(Entity·
ComponentFactory)과 그 주변은 0이지만, `Prefab.h`(6) · `BTBuildGraph.h`(1) ·
`DataSystem.h`(2) · `MaterialAuthoringCodec.h`(2)도 Runtime 표면이다. D3-a-5b가 닫는다.
남은 것 중 `DataSystem.h`는 노드를 **반환**하므로 뷰로는 부족하고 `Document`가 필요하며,
`BTBuildGraph.h`는 헤더 인라인 정의라 `.cpp` 분리가 선행돼야 한다.

★ **직렬화 계층의 노드 타입은 세지 않는다.** `ReflectionYml.h`(12)·
`ReflectionTypedYml.h`(71)와 이번에 만든 Access 헤더들
(`AuthoringNodeViewAccess.h`·`AuthoringDocumentAccess.h`·`AuthoringNodeEquality.h`)·
`EntityAuthoringRead.h`는 **설계상 backend를 아는 계층**이다(완료 기준 8이 "Editor
authoring backend는 ryml 하나"라고 적은 그 계층). 이들의 노드 타입은 D3-b가 backend를
교체할 때 함께 바뀐다. `PrefabOverride.h`·`SerializationProfiler.h`의 등장은 **주석뿐**이라
판정에서 제외한다.

★ **이 슬라이스에는 SFINAE 파손 위험이 없다.** D3-a-4와 달리 여기서 바꾼 것은 훅이
아니라 **직접 호출되는 함수**다 — 호출부가 시그니처와 안 맞으면 컴파일이 실패한다.
그래서 변이 실험 대신 빌드가 곧 증명이다. 실제로 자가 검사 2곳과 호출부 16곳이
컴파일 에러로 드러나 함께 옮겼다.

★ **`iterator_value` 슬라이싱을 주석으로 못 박았다.** `LoadComponent`·
`DesirealizeGameObject`는 원래 yaml-cpp 맵 순회의 `detail::iterator_value`를 받았는데,
`NodeViewAccess::Make`가 `Node` 참조로 받으므로 뷰는 **base 부분**을 가리킨다. 두 함수가
`Node` 연산만 쓰므로 안전하지만, 파생 고유 멤버(`first`/`second`)가 필요해지면 이 창구로는
얻을 수 없다 — 그 사실을 `AuthoringNodeViewAccess.h`에 남겼다.

**검증:** scene authoring 전수 14(28/28) · play round-trip · prefab nested ·
prefab authoring 전수 9/9 · 리플렉션 골든 diff 0 · **DDOL 캔버스 재등록**(DDOL 역직렬화
경로) · UI navigation 전부 통과. Debug x64 CreatorEditor·Player 빌드 exit 0.

**D3-a-5b — 인자형 잔여 — ✅ 부분 완료 (2026-08-30).**
`DataSystem::DeserializeMaterialPayload` · `Prefab::SetPrefabData`의 노드 **인자**를
`NodeView`로 옮겼다. 호출부 11곳(`MeshRenderer`·`PrefabUtility` 2·`DataSystem` 내부 2·
`ConsoleCommandSystem` 5·자가 검사 3).

★ **규칙을 세웠다: 인자는 지금, 반환은 D3-b와 함께.** 노드를 **반환**하는 표면
(`Prefab::GetPrefabData`·`DataSystem::SerializeMaterialPayload`)은 뷰로는 부족하고
`Document`가 필요한데, `Document`는 move-only라 소비자들의 사용 패턴(임시 비교,
`YAML::Node{}` 기본값)이 전부 바뀐다. 그리고 **그 변경은 D3-b가 ryml `Tree`로 갈 때
또 바뀐다.** §1.7 ⑦에서 `TypeOps`를 미리 추상화하지 않기로 한 것과 같은 판단이다 —
같은 코드를 두 번 고치지 않는다.

★ **임시 바인딩을 타입으로 막았다.** `NodeViewAccess::Make(MetaYml::Node&&) = delete`를
추가했다. `Make(node["key"])`나 `Make(YAML::Load(...))`는 뷰가 임시를 가리키는데, 지금은
전체 표현식이 끝날 때까지 임시가 살아 있어 **우연히** 안전하지만 뷰를 한 번만 더
전달하면 깨진다(§8 "ryml view 수명 오용"). 주석 대신 컴파일 실패로 만들었고, **실제로
6곳이 그 자리에서 드러나** named 변수로 고쳤다 — 경고만 적었다면 그대로 남았을 것들이다.

★ **두 대상을 소유권 때문에 뺐다.**
- `MaterialAuthoringCodec.h` — **I5-M5가 소유 중**이다(그 슬라이스의 S0 산출물이고
  S1~S4가 이 코덱의 소비자를 만든다). D1에서 `PackageGameAssets` 제거를
  BuildPipelinePlan E2에 넘긴 것과 같은 이유로 건드리지 않는다.
- `BTBuildGraph.h` — **이 파일은 CP949 인코딩이다**(아래 참조). 게다가 헤더 인라인
  정의라 `.cpp` 분리가 선행돼야 한다.

★★ **저장소에 CP949 소스 73개가 남아 있다(2026-08-30 실측).** UTF-8로 디코딩하면
실패하는 `.cpp/.h/.hpp`가 73개이며, 그중 `Engine\SceneRuntime\ActionMap.cpp`는
**CP949로도 디코딩되지 않는다**(혼합 인코딩으로 보인다). `Engine\Physics\` 전반,
`Engine\SceneRuntime\`의 애니메이션 계열, `BTBuildGraph.h`가 여기 속한다.
이 파일들을 UTF-8로 읽고 쓰면 한글 주석이 통째로 죽는다 — 이 저장소가 이미 한 번 겪은
사고이고, 그 결과가 최근까지 남아 있던 U+FFFD 5,296자였다(별도 세션이 복원 완료).
**D3-b가 backend를 바꾸며 이 파일들을 건드릴 때 인코딩을 먼저 확인해야 한다.**

**검증:** material authoring 왕복 2/2 · scene 전수 14 · prefab 전수 9/9 · prefab nested ·
play round-trip · 리플렉션 골든 diff 0 · UI navigation · `experiment.matmigrate` ·
`experiment.matcodec` · `dx12.selftest`(LivePipelineDesc 8/8, 픽셀 검증 4/4) 전부 통과,
예상 밖 stderr 0. Debug x64 CreatorEditor·Player 빌드 exit 0.

★ **D3-a 밖으로 밀어낸 것 둘.** (a) `m_valueYaml` 저장 표현 — 파일 포맷이라 D5/트랙 P
소유(§1.7 ⑧). (b) `Entity::m_prefabOriginal` 제거 — 비직렬화 과도기 시딩 근거이고
SceneGraphRedesignPlan 트랙 P(P1 "과도기 시딩 허용")의 소유다. 소비자 6곳이 살아 있어
D3-a가 임의로 지우면 시딩이 죽는다. D3-a-3은 이 필드를 **감싸기만** 하고 없애지 않는다.

**D3-b-0 — ryml 도입 + 파서 동등성 프로브 — ✅ 완료, 그리고 D3-b의 전제를 흔든다
(2026-08-30).**

`vcpkg.json`에 `ryml` 0.16.0을 되살렸다 — **새 도입이 아니라 복귀**다. PHASE 4-6이
"흔적 0"으로 걷어낸 포트 10개 중 하나였고, 그 사실이 매니페스트 주석에 남아 있다.
매니페스트 모드라 빌드가 자동 설치했고(triplet `x64-windows`, `-idl0`는 physx에 막혀
좌초한 과거 시도의 잔재다) include·링크 모두 추가 설정 없이 통과했다.

전환에 앞서 **"ryml이 이 저장소의 저작 문서를 같게 읽는가"를 먼저 쟀다**
(`Engine/Utility_Framework/AuthoringParserProbe.h/.cpp`, CLI `serialize.parsercompare`).
408매치를 옮기고 나서 어긋나면 파서 차이와 이행 실수를 가를 수 없기 때문이다.

**전수 결과 (저작 코퍼스 292파일) — Debug와 Release가 정반대다:**

| 항목 | Debug x64 | **Release x64 (정본)** |
|---|---:|---:|
| 구조 동등 | 255 | **278** |
| 구조 상이 | 23 | **0** |
| ryml 파싱 실패 | 14 | **0** |
| 비교한 노드 | 3,461 | **15,336** |
| YAML 아님(건너뜀) | — | 14 (모델 `CEMA` 바이너리) |
| CRLF 정규화 필요 | 254 / 292 | 241 / 292 |
| yaml-cpp 총 파싱 | 1,097.0 ms | **83.6 ms** |
| ryml 총 파싱(정규화 포함) | 812.2 ms | **6.58 ms** |
| **배율** | 1.35× | **12.70×** |

★★ **Debug 수치가 이득을 9배 과소평가했고, 나는 그것으로 슬라이스를 접을 뻔했다.**
처음 이 절을 쓸 때 제목이 "D3-b의 전제를 흔든다"였고, 근거는 "배율 1.35×"였다.
`[[perf-measure-release-only]]`를 알면서도 Debug 수치로 결론의 방향을 정한 것이다.
Release에서는 **12.70×**로 통념과 일치한다. **성능으로 계획을 바꾸려면 Release가
먼저다 — 단서를 다는 것으로는 부족하고, 재측정 전에는 결론을 쓰지 않는 편이 옳다.**

**의미:** D0는 씬 전환 30.203 ms 중 `SceneParse`가 19.351 ms(60.0%)라고 말했다.
12.70×면 그 몫이 약 1.5 ms가 되어 **씬 전환 30.2 ms → 약 12.3 ms(−59%)** 다.
§5 완료 기준 2의 문턱(≥35% 감소)을 크게 넘는다. **D3-b는 값을 한다.**

★ **구조 상이 23건과 파싱 실패 14건은 ryml의 한계가 아니라 내 프로브의 결함이었다.**
- 상이 23건: 저작 문서의 `m_prefabOverrides: ~`를 yaml-cpp만 널로 읽었다. ryml에는
  `val_is_null()`이 있는데 내가 `val() == nullptr`로 판정해 **비교기가 비대칭**이었다.
- 실패 14건: 전부 `Assets\Models\*.asset`이고 앞 4바이트가 `CEMA`인 **모델 바이너리
  캐시**다. **`.asset` 확장자가 두 포맷을 담는다** — 재질은 평문 YAML, 모델은 바이너리.
  확장자만 보고 파서에 넣으면 yaml-cpp는 쓰레기를 만들고 ryml은 프로세스를 죽인다.
  이제 매직으로 걸러내되 `skippedBinary`로 보고한다 — 건너뛴 것은 "문제없음"이 아니라
  "확인하지 않음"이다.

★ **남는 진짜 제약은 둘이다.**
1. ~~**ryml은 CRLF에서 프로세스를 abort한다.**~~ **틀렸다 — D3-b-1이 반증했다.**
   착수 시점에 관찰한 abort 메시지(`check failed: rem.find(CR) == npos`)를 CRLF 탓으로
   읽었는데, 14종을 각각 태워 보니 **CRLF는 ryml 0.16이 정상 파싱한다**(단순 맵·
   시퀀스·블록 스칼라·folded·주석 모두). 실제 트리거는 **홀로 선 CR**이다. 따라서
   프로브가 넣은 정규화 사본은 불필요하고, 위 **12.70×는 그 사본 비용을 포함한
   보수적 하한**이다. 원인을 재지 않고 인접한 관찰로 지목한 오진이었다.
2. **ryml의 에러 처리가 abort다.** 0.16은 콜백이 `m_error_basic`/`m_error_parse`/
   `m_error_visit` 셋으로 나뉘어 하나만 덮으면 나머지 경로에서 여전히 죽는다.
   **제품 경로 투입의 선결 조건**이다 — 저작 문서 하나가 잘못돼도 에디터가 그 자리에서
   죽어서는 안 된다.

★ **ryml의 에러 처리는 예외가 아니라 abort다.** 기본 콜백이 그렇고, 0.16은 콜백이
`m_error_basic`/`m_error_parse`/`m_error_visit` 셋으로 나뉜다 — 하나만 덮으면 나머지
경로에서 여전히 죽는다(실제로 처음 두 실패가 각각 basic·parse였다). **ryml을 제품
경로에 넣는다면 이 콜백 설치가 선결 조건이다.** 저작 문서 하나가 잘못돼도 에디터가
그 자리에서 죽어서는 안 된다.

**세 항목 모두 해소됐다(2026-08-30).** ① Release 재측정 12.70× ② 상이·실패는 프로브
결함으로 판명, 고친 뒤 0/0 ③ 재검토 결과 **D3-b는 계속한다.**

★ **D5와의 관계를 분명히 해 둔다.** D5(쿠킹)는 **Player**의 파싱을 0으로 만들고,
D3-b는 **에디터 저작** 경로를 12.70× 빠르게 한다. 겹치지 않는다 — 에디터는 텍스트를
계속 읽어야 하고(그것이 저작의 정의다), Player는 텍스트를 읽지 않아야 한다. 둘은
대체재가 아니라 각자의 경로를 맡는다.

**D3-b-CRLF — 저작 개행을 LF로 고정 — ✅ 완료 (2026-08-30).**
선결 조건 둘 중 하나를 닫았다. **정규화가 아니라 LF 통일을 골랐다.**

★ **결정적 근거: git index는 이미 LF였다.** `.gitattributes`의 `* text=auto`가 커밋 시
정규화하고 있어서 추적 자산의 blob은 전부 LF이고, **작업 트리만 CRLF/LF로 혼재**했다.
즉 이 변경은 저장소 내용을 바꾸는 것이 아니라 **작업 트리를 저장소와 일치시키는 것**이다
— `git add --renormalize` 결과 개행으로 인한 staged 변경이 **0**이었다.

**고른 이유 넷:**
1. **혼재가 불안정의 원인이었다.** index는 LF인데 작업 트리 바이트가 달라
   `git status`는 더럽고 `git diff`는 비는 상태(`[[atomic-writer-line-endings]]`가 기록한
   그 증상)다. 그때 대응이 거꾸로였다 — 이진 모드 LF 쓰기가 맞고, `eol=lf`를 주지 않은
   것이 문제였다.
2. **content-addressed VCS로 가면 정규화 계층이 사라진다.** `text=auto`는 Git 고유
   기능이고, 내용 해시로 식별하는 시스템(Epic **Lore** 등)에서는 CRLF와 LF가 곧 다른
   콘텐츠다. LF는 플랫폼 중립이라 어느 VCS로 가도 안정적이다.
3. **ryml 제약이 사라진다.** 파싱 전 정규화 사본이 불필요해지므로 12.70× 배율이
   더 오른다(그 배율은 정규화 비용을 포함한 값이었다).
4. Unity 등 업계 관행과 일치한다(`*.unity`/`*.meta`에 `eol=lf`).

**한 일:** `.gitattributes`에 저작 확장자별 `text eol=lf` 명시 · 저작 텍스트 자산
**243개를 CRLF → LF 변환**(37개는 이미 LF, 14개는 `CEMA` 바이너리로 제외) ·
텍스트 모드 writer **11곳을 `ios::binary | ios::trunc`로** 전환.

★ **`.asset` 확장자가 두 포맷을 담아 `.gitattributes`에 명시가 필요했다.**
`Models/**/*.asset`은 `CEMA` 바이너리, `Materials/**/*.asset`은 평문 YAML이다.
`* text=auto`는 내용을 보고 **추측**하는데, 지금은 모델 쪽 널 바이트 덕에 우연히
binary로 판정될 뿐이었다. 텍스트로 오판되면 개행 정규화가 바이너리를 깨뜨린다.

★ **핵심은 writer였다.** `EditorAssetDatabase`의 게시 경로가
`PublishEncoding::Binary`가 아닐 때 `std::ios::trunc`만 주어 **텍스트 모드로 열고**
있었다. 인코딩 구분은 *무엇을 쓰는가*이지 *개행을 어떻게 쓰는가*가 아니므로, 두 경로
모두 binary로 열도록 고쳤다.

**게이트:** `verify-authoring-line-endings.ps1`(run-all 편입). **결과와 원인을 함께**
본다 — 자산의 CRLF 0 · `.gitattributes`의 `eol=lf` 규칙 · writer의 텍스트 모드 0.
결과만 재면 "지금은 깨끗하지만 다음 저장에서 되돌아오는" 상태를 통과시킨다.
착수 전 RED(CRLF 241 / writer 11 / 규칙 6건 누락) → 착수 후 GREEN을 확인했다.

★ **게이트가 처음 두 곳을 오탐했다.** 한 줄만 보면 멀티라인 `ofstream` 선언과
변수 모드(`ofstream(path, mode)`)를 텍스트 모드로 오판한다. 선언문을 세미콜론까지
이어붙이고, 모드 변수가 binary로 정의됐는지 확인하도록 고쳤다.

★ **내 편집이 개행을 혼합시켰다(자기 정정).** 스크립트로 CRLF 파일에 `\n` 줄을
삽입해 22개 파일이 CRLF/LF 혼합이 됐고, 주석 안에 넣으려던 이스케이프가 실제 개행이
되어 **주석이 줄을 끊고 빌드를 깨뜨렸다**(5곳). 지배적 개행으로 통일하고 주석을
복구했다. **CRLF 파일을 스크립트로 편집할 때는 삽입 문자열의 개행을 파일과 맞춰야
한다** — `[[cp949-source-edit-corruption]]`의 개행판이다.

**검증:** scene authoring 전수 14 · prefab 전수 9/9 · material 왕복 2/2 · 리플렉션
골든 diff 0 · BT smoke 통과, 그리고 **왕복 후에도 자산 CRLF 0**(writer 실동작 판정).
Debug x64 CreatorEditor·Player 빌드 exit 0.

**D3-b-1 — ryml 에러 정책을 제품 경로에 설치 — ✅ 완료 (2026-08-30).**

ryml의 기본 에러 처리는 예외도 반환값도 아니라 **프로세스 abort**다. 즉 backend를
바꾸는 것만으로 **"로드 실패"가 "프로세스 사망"으로 승격된다.** yaml-cpp는 같은
입력에 예외를 던진다. 그래서 파서를 옮기기 전에 이 승격을 되돌리는 것이 선결 조건이었다.

`AuthoringRymlErrorPolicy.h/.cpp`가 정본이다. 헤더에 ryml 타입이 없고
(`AuthoringDocument.h`와 같은 규율), `.cpp`가 basic/parse/visit **세 채널을 모두**
`std::runtime_error`로 바꾼다. D3-b-0 프로브가 들고 있던 사본은 지우고 이 정책을
부르게 했다 — 프로브와 제품이 서로 다른 정책으로 갈라지면 프로브가 통과해도 제품이
abort하는 상태를 몰라볼 수 있다.

**★ 범위 RAII가 아니라 전역 1회 설치다.** `ryml::set_callbacks`는 프로세스 전역을
쓴다. 파싱마다 설치·복원하면 씬 로드의 배치 경로처럼 여러 스레드가 동시에 파싱할 때
한 스레드의 복원이 다른 스레드의 설치를 지운다 — 그 순간에만 abort하므로 재현되지
않는다. `[[nonrepro-failure-was-thread-race]]`가 이미 한 번 가르쳐 준 형태다.

**★ 실측이 내 재현 둘을 반증했다.** 처음 게이트를 만들 때 트리거로 "CRLF"와
"멀티라인 스칼라 키"를 썼는데 **ryml이 둘 다 조용히 받아들였다** — 게이트가 초록인데
아무것도 증명하지 않는 상태였다. 그래서 입력을 밖에서 주는 창구
(`TryParseWithPolicy`)를 만들고 14종을 각각 별도 프로세스로 태웠다:

| 입력 | 결과 |
|---|---|
| CRLF 단순 맵 · 시퀀스 · 블록 스칼라(`\|`) · folded(`>`) · 주석 | **모두 정상 파싱** |
| 멀티라인 스칼라 키(`? >`, `? \|`) | **정상 파싱** |
| 중복 키 · 없는 앵커 · 잘못된 태그 | 정상 파싱 |
| **홀로 선 CR** | throw `[basic] check failed: rem.find(CR) == npos` |
| **탭 들여쓰기** | throw `[parse] parse error` |
| 닫히지 않은 flow `[1, 2` | throw `missing terminating ]` |
| CRLF + 여러 줄 겹따옴표 스칼라 | throw `bad indentation` |

**즉 "CRLF는 ryml이 abort한다"는 과잉 일반화였다.** D3-b-0 프로브가 파싱 전 정규화
사본을 넣고 그 비용을 ryml 몫에 포함시킨 근거가 이것이었는데, **CRLF는 정상 파싱되므로
그 사본은 불필요하다.** 실제 트리거는 홀로 선 CR이다. 12.70×는 정규화 비용을 포함한
수치이므로 **보수적인 하한**이고, 실제 배율은 그보다 크다.

에러 메시지 앞에 채널 태그(`[basic]`/`[parse]`/`[visit]`)를 붙였다. what() 문자열만
보면 어느 채널을 타고 왔는지 알 수 없어, 게이트가 "여러 채널을 덮었다"를 단정하지
못한다.

**게이트 `verify-ryml-error-policy.ps1`** — 이 검사의 이빨은 종료 코드가 아니라
**크래시**다. 채널 하나만 빠져도 명령은 "fail"을 찍는 것이 아니라 프로세스가 죽는다.
**변이 2회로 확인했다:** `m_error_basic` 제거 → exit 3(abort), `m_error_parse` 제거 →
exit 3. 둘 다 게이트가 잡았다(요약 라인 부재 + 종료 코드). 대조군 둘도 함께
단정한다 — 정상 문서가 읽히는가(정책이 파싱 자체를 막는 상태 배제), **CRLF 문서가
읽히는가**(이것이 깨지면 정규화 사본이 다시 필요해지고 성능 계산이 바뀐다).
`run-all.ps1`에 편입.

**★ `m_error_visit`은 검사가 덮지 못한다.** 그 채널은 파싱이 아니라 트리 순회에서
열리므로 파서 입력만으로 닿지 않는다. 셋을 모두 설치하되 검사는 둘만 덮는다는 사실을
숨기지 않는다 — D3-b-2가 ryml 노드를 실제로 순회할 때 그 채널 검사를 함께 만든다.

---

**D3-b-1 이후의 순서를 정정한다 — `Document::Impl` 교체는 첫 단계가 아니라 마지막
단계다.**

착수 전 기록은 "다음: `Document::Impl`을 ryml `Tree`로 바꾼다"였다. **그것을 재 보니
틀렸다.** 두 가지 이유이고 둘 다 실측이다.

**① `Document`는 파싱 경로에 없다.** `Adopt` 호출부 전수 5곳이 전부 **메모리 안의
yaml-cpp 노드**를 받는다 — 파일 파싱 결과가 아니다.

| 호출부 | 넘기는 값 |
|---|---|
| `SceneManager.cpp:1298` | `Meta::Serialize(scene)` |
| `Prefab.cpp:194` | `SerializeRecursive(source, guid)` |
| `Prefab.cpp:215` | `NodeViewAccess::Node(view)` |
| `Prefab.cpp:524` · `PrefabUtility.cpp:616` | `MetaYml::Clone(node)` |

그래서 `Impl`만 ryml로 바꾸면 `Adopt`마다 yaml-cpp→ryml 변환이 생긴다. **파싱 이득은
0인데 변환 비용만 붙는다.** 게다가 그 변환을 없애려면 `Meta::Serialize`가 ryml을
반환해야 하고, 그건 리플렉션 계층 전체(yaml-cpp 등장 420곳)를 한 번에 옮기는 것이다.

**② 측정된 이득은 전부 `LoadFile` 쪽에 있다.** D0이 잰 `SceneParse`는 씬 로드의
60.0%이고, 그 몫은 `MetaYml::LoadFile` 4곳(SceneManager)과 나머지 파싱 지점
**약 40곳**에 있다. `Document`는 그중 어디에도 없다.

**따라서 D3-b는 읽기 경로부터 간다.** 읽기와 쓰기는 갈라져 있고, 이득은 읽기에만 있다.

**D3-b-2 착수 전 실측 — 어디에 이득이 있는지 확장자별로 갈랐다 (2026-08-30, Release).**

`serialize.parsercompare`에 확장자 필터를 붙였다. 전체 합계만으로는 "부팅 catalog의
53.5 ms 중 파싱 몫이 얼마인가"에 답하지 못한다.

| 대상 | 파일 | yaml-cpp | ryml | 배율 |
|---|---:|---:|---:|---:|
| `.meta` 전수 | 240 | 15.063 ms | 2.092 ms | 7.20× |
| `.creator` 전수 | 14 | 65.108 ms | 4.308 ms | 15.11× |
| `.prefab` 전수 | 9 | 3.822 ms | 0.450 ms | 8.49× |
| **Test1.creator 단독** (80,661 B) | 1 | **37.557 ms** | **1.990 ms** | **18.87×** |

**① 부팅 catalog에서 파싱은 소수다 — ryml만으로는 §5 목표에 못 미친다.**
`.meta` 240개 파싱은 **15.063 ms로 catalog 53.560 ms의 28.1%뿐**이다. ryml로 바꿔도
12.97 ms 절약, **24% 감소**에 그친다(목표는 ≥80%). 나머지 **72%는 디렉터리 재귀 +
`file::exists` + 레지스트리 삽입**이다. **그러므로 `.meta`를 ryml로 옮기는 별도
슬라이스는 만들지 않는다** — 목표를 못 채우고, D5-c의 manifest가 대체할 대상이다.
**동시에 D5-c에 대한 요구가 하나 늘었다: manifest는 파싱만 없애서는 안 되고
디렉터리 순회 자체를 없애야 한다.** 그것이 72%다.

**② 씬 읽기 경로는 §5 완료 기준 2를 단독으로 넘긴다.** D0의 Test1 `SceneParse`는
19.351 ms(SceneLoadTotal 32.239 ms의 60.0%)다. 배율이 5×만 돼도 15.5 ms를 회수해
**48% 감소**, 10×면 **54%**다 — 목표 ≥35%를 넓은 범위에서 넘는다. **D3-b-2가
D3-b의 값을 혼자 낸다.**

★ **프로브 수치와 D0 수치가 어긋난다 — 배율을 액면가로 쓰지 않는다.** 같은
Test1.creator를 프로브는 yaml-cpp 37.557 ms로 재는데 D0의 `SceneParse`는 19.351 ms다
(1.94배). 프로브는 **항상 yaml-cpp를 먼저** 돌리므로 할당자·캐시 워밍업 비용을
yaml-cpp가 떠안는 편향이 있다. 그래서 18.87×는 상한이고, 위 ①②의 결론은 **배율이
5×까지 떨어져도 유지되도록** 세웠다. 최종 판정은 프로브가 아니라 **제품 경로의
`SceneParse` A/B**(D0과 같은 profiler)가 낸다.

**D3-b-2a — 쓰기 뷰를 타입으로 분리 — ✅ 완료 (2026-08-30).**

`NodeView` 하나가 읽기와 쓰기를 겸하고 있었다. 그래서 읽기 backend를 ryml로 바꾸면
쓰기 훅이 함께 끌려가 D3-b-3를 앞당겨야 했다 — 이득이 없는 쪽까지 같은 슬라이스에
묶이는 구조였다. `Authoring::MutableNodeView`/`MutableNodeViewAccess`로 갈랐다.

**대상이 실측상 세 곳뿐이라 비용이 거의 없다:** `Entity::OnAfterSerialize` 훅 하나,
`Scene::SerializeEntityHierarchy` 하나, 리플렉션 썽크의 `requires` 하나. 그 대가로
읽기와 쓰기가 독립적으로 움직인다.

★ **SFINAE 훅은 컴파일되면서 조용히 꺼진다.** 훅 시그니처를 바꾸는 변경은 빌드가
통과해도 아무것도 증명하지 못한다 — `requires`가 안 맞으면 분기가 사라질 뿐이다.
그래서 **변이로 발화를 증명했다:** 썽크의 `MutableNodeView` 분기를 못 맞게 바꾸자
`verify-scene-authoring-corpus`(CLI 보고 실패 28) ·`verify-play-roundtrip`
(restored=1) ·`verify-prefab-nested`(rootref 미연결) **셋이 빨개졌다.**
**`verify-reflection-golden`은 초록으로 남았다** — 골든은 Entity를 기본 생성하므로
`m_ownerScene`이 널이고 훅이 즉시 반환한다. 이 훅을 지키는 것은 골든이 아니라 저 셋이다.

---

**D3-b-2b-0 — 스칼라 변환 파리티 — ✅ 완료, 그리고 11건이 실제로 갈린다
(2026-08-30).**

D3-b-0의 파서 프로브는 스칼라를 **문자열로만** 비교했다. 그것으로 증명되지 않는
것이 이것이다: `node.as<bool>()`와 ryml의 변환이 같은 답을 내는가. 같은 문자열
`"yes"`를 다르게 읽으면 **트리는 같은데 값의 의미만 조용히 달라진다** — 로드는
성공하고 값만 틀린다. 파서를 옮긴 뒤 가장 늦게, 가장 비싸게 드러나는 종류다.

변환 경계는 좁다. `ReadScalar` 오버로드 15종이 실제로 부르는 것은
`as<std::string>`·`as<float>`·`as<bool>`·`as<size_t>`·`as<int>`·`as<uint32_t>`
여섯이다. 44개 케이스로 그 여섯을 쟀고 **11건이 갈렸다.**

| 케이스 | yaml-cpp | ryml | 성격 |
|---|---|---|---|
| `.inf` · `-.inf` · `.nan` | 읽는다 | **거부** | ryml이 좁다 |
| `yes` · `no` · `on` · `off` (bool) | 읽는다(YAML 1.1) | **거부** | ryml이 좁다 |
| `1` · `0` (bool) | **거부** | 읽는다 | **방향이 뒤집힘** |
| `0o10` (8진) | 거부 | 읽는다 | ryml이 넓다 |
| `~`를 문자열로 | `"null"` | `"~"` | **값이 다르다** |

**★ 갈리는 것과 위험한 것은 다르다.** 차이가 손상이 되려면 코퍼스에 그 표기가
있어야 한다. 자산 278개를 스캔했다:

- `.inf`/`.nan`/`0o` — **0건.** 위험 없음.
- YAML 1.1 불리언 — **2건**(shadermeta). 게이트가 이 수를 기준선으로 고정한다.
- `~` — **390건 / 23파일.** 많다. 그러나 필드를 세어 보니 전부 컨테이너·포인터다
  (`m_prefabOverrides` 212 · `m_Material` 29 · `m_Mesh` 29 · `m_childrenIndices` 26 …).
  `ReadMember`의 포인터 분기는 `if (!sub.IsMap()) return;`, 벡터 분기는
  `IsSequence()`로 걸러 **`ReadScalar(std::string&)`에 닿지 않는다.** 즉 이 차이는
  현재 코퍼스에서 죽어 있다 — 그러나 **문자열 필드가 널을 갖는 순간 살아난다.**

**게이트 `verify-scalar-conversion-parity.ps1`은 "차이 0"을 단정하지 않는다.**
차이를 0으로 만드는 것은 D3-b-2b의 일(명시적 변환을 넣는 것)이고, 이 검사의 일은
**알려진 목록을 고정**해 새 차이만 빨개지게 하는 것이다. 목록보다 **적어도** 실패다 —
차이가 사라졌다면 누가 변환을 바꾼 것이고 표가 낡는다. 변이 2회로 양방향을
확인했다(목록에서 제거 → "새로 갈린 케이스", 없는 항목 추가 → "더 이상 갈리지
않는 케이스"). 코퍼스 스캔도 같은 게이트에 있다. `run-all.ps1` 편입.

**D3-b-2b가 해야 할 것이 이 표로 확정됐다.** `ReadScalar`를 ryml로 옮길 때
`from_chars`를 그대로 쓰면 안 된다 — bool과 float는 **명시적 변환**을 거쳐야
yaml-cpp 의미를 유지한다. 부재 키(`if (!sub)`)도 ryml에서는 `has_child`가
선행돼야 하며(없는 키를 `operator[]`로 만지면 visit 채널로 죽는다),
"정의됐지만 널"은 `val_is_null()`로 따로 봐야 한다.

- **D3-b-2 (다음):** 씬 로드의 읽기 경로를 ryml로. `LoadFile` → `parse_in_arena`,
  그리고 그 결과를 소비하는 typed 역직렬화기(`ReadMember`·`DeserializeObjectFrom`)와
  `NodeView` 훅 본문. `NodeView`의 **선언**은 이미 포맷을 모르므로 시그니처는 그대로다
  — D3-a가 만든 경계가 값을 하는지가 여기서 드러난다. 판정: D0과 같은 workload의
  Release A/B에서 `SceneParse` 감소 + 씬 전수 로드 통과.
**D3-b-2b-1a — 스칼라 변환을 backend에서 분리 — ✅ 완료 (2026-08-30).**

`Authoring::Scalar`(`AuthoringScalarConvert.h/.cpp`)가 정본이다. 값 변환은 **문자열
위의 함수**가 하고 노드는 원문을 꺼내는 데만 쓴다. 그래서 D3-b-2b-1b가 backend를
바꿔도 값의 의미가 바뀌지 않는다.

**★ 재구현이 아니라 이식이고, 그 차이를 게이트가 강제한다.** 의미를 머리로 다시
짜면 어긋난다 — 실제로 **이식 오류 3건을 게이트가 잡았다**:
- `as<std::uint64_t>("-1")`: yaml-cpp는 **실패**하는데 내 `operator>>`는 래핑된
  최대값으로 성공했다. 코드 주석에 "yaml-cpp도 같은 스트림을 쓰니 동작이 같을
  것"이라 적어 두었는데 **그 추측이 틀렸다.**
- `v: ~`와 `v: null`: yaml-cpp `as<std::string>`은 **"null"**을 준다. 널 노드는
  `IsScalar()`가 거짓이라 원문 추출이 실패했다. 이것은 변환이 아니라 **노드→원문
  추출**의 규칙이며, ryml 쪽에서 `val_is_null()`로 같은 규칙을 세워야 한다.

**검증: 67케이스 전수에서 이식 변환기 대 yaml-cpp 차이 0(`convDiverge=0`).**
케이스를 44 → 67로 넓혔다(진법 접두사·선행 0·부호·부분 파싱·오버플로·좁은 타입).

**★ 그 확장이 ryml의 가장 위험한 차이를 드러냈다 — 실패가 아니라 "둘 다 성공하는데
값이 다른" 쪽이다.**

| 입력 | yaml-cpp | ryml |
|---|---|---|
| `010` | **8** (8진) | **10** (10진) |
| `99999999999999999999999` | 실패 | **200376420520689663** (쓰레기) |
| `1.5x` | 실패 | **1.5** (부분 파싱) |
| `+42` · `+1.5` | 읽음 | 거부 |

에러도 로그도 없이 값만 틀린다. **그러므로 D3-b-2b-1b는 ryml `from_chars`를 직접
쓰면 안 된다.** ryml 차이는 11 → **21건**으로 늘었고, 게이트가 그 목록을 고정한다.

**배선했다 — 죽은 코드로 두지 않았다.** `ReadScalar`의 산술 오버로드가 변환기를
쓴다. 실패 경로는 `as<T>()`에 남겨 예외 타입·메시지를 그대로 유지한다(변환기와
yaml-cpp의 성공 판정 일치를 게이트가 전수 단정하므로, 이 폴백은 **yaml-cpp도 실패할
때만** 돈다). `char` 계열은 제외했다 — `as<char>`는 숫자가 아니라 문자 하나를 읽는다.

**★ 변이로 배선이 살아 있음을 증명했다.** `TryParseFloat`에 `+1.0f`를 넣자
`verify-scene-authoring-corpus`가 씬 4개에서 빨개졌다. **`verify-reflection-golden`은
초록으로 남았다** — 골든은 직렬화 골든이라 역직렬화 변환을 지키지 못한다.
`OnAfterSerialize` 때와 같은 사각이며, 여기서도 코퍼스 게이트가 실제 보호자다.

★ 게이트 범위 누락 하나를 함께 닫았다. 개행 게이트가 `ProjectSetting/`을 안 봐서
거짓 초록을 낸 전례가 있어, 이 게이트의 코퍼스 스캔도 두 루트를 보게 했다(278 → 281).

**다음(D3-b-2b-1b):** 트리 순회를 ryml로. 변환은 이미 backend를 모르므로 남은 것은
`LoadFile` → `parse_in_arena`, `NodeView`의 backing, `ReadMember`의 부재 키 판정
(`has_child` 선행), 그리고 널 노드 규칙(`val_is_null()` → "null")이다.

**D3-b-2b-1b-1 — 읽기 어댑터 도입 + 타입 디시리얼라이저 이관 — ✅ 완료 (2026-08-30).**

`Authoring::ReadNode`(`AuthoringReadNode.h`)가 읽기 경로의 노드 창구다.

**★ 왜 어댑터를 먼저 세우는가.** 파서를 바꾸려면 `LoadFile`부터 `ReadMember`까지
타입이 한꺼번에 바뀌어야 한다 — 중간에 끊을 수 없어 수백 곳을 한 번에 고치게 되고,
그러면 어긋났을 때 **"ryml이 다르게 읽은 것"과 "옮기다 틀린 것"을 가를 수 없다.**
D3-b-0이 파서 프로브를 먼저 만든 것과 같은 이유다. 지금 단계의 판정은 성능이 아니라
**"행동이 하나도 안 바뀌었다"** 이고, 기존 게이트가 그것을 그대로 잰다.

**표면은 실측으로 정했다 — 아홉 가지뿐이다.** 타입 디시리얼라이저가 쓰는 것은
유효성·널·스칼라·맵·시퀀스·크기·키 조회·시퀀스 순회·원문이다. **맵 순회는 없다**
— 그것은 소비자(SceneManager·ComponentFactory)의 것이고 다음 조각이다.

옮긴 것: `ReadMember` · `DeserializeObjectFrom` · `ReadScalar` 전 오버로드 ·
`DeserializeThunk` · `PostLoadThunk` · `TypeOps::deserialize`/`postLoad` 시그니처.
enum도 `as<int>` 직결 대신 변환기를 태운다 — 직결은 backend 의미에 묶인다.

★ **순환 include를 하나 만들었다가 고쳤다.** 어댑터가 `ReflectionYml.h`를 물고
그쪽이 어댑터를 물었다. 어댑터가 yaml-cpp를 직접 물게 해서 끊었다(네임스페이스
별칭은 같은 대상이면 중복 선언이 허용된다).

**★ 전환기 탈출구를 이름으로 드러냈다.** 아직 옮기지 않은 소비자가 backend 노드를
필요로 하므로 `BackendNodeDuringTransition()`을 뒀다. **잔존은 한 파일 6곳뿐**이고
(`ReflectionTypedYml.h`: 변환 폴백 3 · `char` 경로 1 · enum 폴백 1 · `NodeView`
브리지 1), 전부 의도된 지점이다. 이름이 길고 흉한 것이 목적이다 — 남아 있으면
전환이 덜 끝난 것이고, 개수가 진행률이다.

**검증:** Debug x64 CreatorEditor·Player 빌드 exit 0, 게이트 9종 exit 0
(골든 diff 0 · scene 전수 14 · prefab 9/9 · material 2/2 · play 왕복 · 중첩 프리팹 ·
BT smoke · 스칼라 파리티 · 구조 비교). **행동 변화 0.**

**다음(D3-b-2b-1b-2):** 소비자(`SceneManager`·`ComponentFactory`·훅 본문)를 어댑터로
옮기고 **맵 순회**를 표면에 더한다. 그 뒤에 backend를 ryml로 바꾼다 —
`LoadFile` → `parse_in_arena`, 없는 키는 `has_child` 선행(ryml은 `operator[]`로
만지면 visit 채널로 죽는다), 널은 `val_is_null()` → `"null"`.

**D3-b-2b-1b-2a — 맵 순회 + 타입 판별 이관 — ✅ 완료 (2026-08-30).**

어댑터에 **맵 순회**(`MapRange`/`MapEntry`)와 `HasChild`를 더하고, 읽기 경로에서
가장 미묘한 함수인 `Meta::ExtractTypeFromYAML`을 옮겼다(UUID → 이름+typeID →
맵 폴백 → `typeID` 필드, 네 단계).

★ **맵은 인덱스로 못 돈다.** yaml-cpp의 `operator[](size_t)`는 맵에서 **인덱스가
아니라 키**로 해석되어 조용히 엉뚱한 값을 준다. 그래서 시퀀스와 달리 별도 반복자를
뒀다. ryml은 인덱스 접근이 되지만 같은 인터페이스를 내주는 편이 옮길 때 안전하다.

★ **`MapRange`는 노드를 값으로 소유한다.** 참조로 잡으면
`for (auto e : node.Map())`에서 임시가 먼저 죽어 반복자가 dangling이 된다.

★ **중첩 타입이 불완전 타입을 담을 수 없어 맵 타입을 클래스 밖으로 뺐다**
(`MapEntry`가 `ReadNode`를 값으로 담는다).

`as<T>(0)` 폴백 형태는 변환기 + 기본값으로 옮겼다 — 던지지 않는 것이 요점이다.
숫자가 아니면 타입 헤더가 아니라 이름이 우연히 겹친 데이터 필드다.

**★ 옮긴 분기가 실제로 도는지 변이로 확인했다.** UUID 경로(0단계)가 항상 이기면
1단계 맵 순회는 **검증되지 않은 코드**가 된다. 1단계를 막자
`verify-scene-authoring-corpus`(씬 4개 + `vector subscript out of range`)와
`verify-prefab-authoring-corpus`(snapshots 1/2)가 빨개졌다 — 살아 있고 필수다.
`verify-reflection-golden`은 여기서도 초록으로 남았다(직렬화 골든).

**검증:** Debug x64 CreatorEditor·Player exit 0, 게이트 10종 exit 0. 행동 변화 0.

**남은 전환기 표면:** `BackendNodeDuringTransition` 6곳(`ReflectionTypedYml.h`) +
`ExtractTypeFromYAML`의 backend 오버로드 1개(호출부 15곳). 그 오버로드가 사라지는
것이 소비자 이관 완료의 신호다.

**다음(D3-b-2b-1b-2b):** `SceneManager::Desirealize*`·`ComponentFactory` 본문과 훅을
어댑터로 옮긴다. 그 뒤 backend 교체 — `parse_in_arena`, `has_child` 선행,
`val_is_null()` → `"null"`, 그리고 `NodeView`의 backing을 불투명 2워드로.

**D3-b-2b-1b-2b — 훅 인자를 어댑터로 + backend 경계 래칫 — ✅ 완료 (2026-08-30).**

`NodeViewAccess::Node()`가 **어댑터를 값으로 돌려준다.** 이전에는 backend 노드
참조였는데, 그러면 backend를 바꾸는 순간 훅 본문 전부가 함께 깨진다. 어댑터를
돌려주면 훅 본문은 `ReadNode` 연산만 쓰게 되고 **backend 교체가 이 함수 한 줄로
좁혀진다.**

변환 디스패치(`TryConvert`)를 `Authoring::Scalar`로 승격해 어댑터와 리플렉션이
같은 것을 쓰게 했다. 어댑터에 `As<T>()`·`As<T>(fallback)`·`AsString()`을 더했다 —
yaml-cpp `as<T>()`의 **드롭인 대체**이고, 실패하면 backend가 그대로 던진다(예외
타입·메시지가 같아야 `LoadScene`의 catch 의미가 유지된다).

**소비자 9곳은 계수 가능한 전환기 패턴으로 남겼다.** 본문이 아직 backend API에
묶여 있고(특히 `DataSystem`은 experiment 코덱에 backend 노드를 넘긴다 — I5 소유),
한 번에 옮기면 실패 원인을 못 가른다.

**★ 그래서 래칫 게이트를 만들었다 — `verify-authoring-backend-boundary.ps1`.**
탈출구 **15곳** / `ExtractTypeFromYAML` backend 호출부 **14곳**이 기준선이다.

이 게이트가 막는 것은 실패가 아니라 **역행**이다. 새 코드가 어댑터 대신 backend
노드를 직접 잡으면 **빌드도 다른 게이트도 전부 통과하므로 아무도 모른다.** 기준선을
넘으면 실패하고, 줄면 기준선을 갱신하라고 말하며(숫자가 낡으면 래칫이 풀린다),
**0이 되면 이 게이트를 은퇴시키라고 말한다.** 변이로 확인했다(가짜 탈출구 1개 추가
→ 16/15 실패, 제거 → 통과). `run-all.ps1` 편입.

이름이 흉한 것이 목적이다: `BackendNodeDuringTransition`. **개수가 곧 진행률이다.**

**검증:** Debug x64 CreatorEditor·Player exit 0, 게이트 8종 exit 0. 행동 변화 0.

**다음:** 소비자 본문을 하나씩 어댑터로 옮겨 래칫을 내린다(SceneManager 3 ·
ComponentFactory 1 · 훅 4 · 리플렉션 6). 그 뒤 backend 교체 —
`parse_in_arena` · `has_child` 선행 · `val_is_null()` → `"null"` ·
`NodeView` backing을 불투명 2워드로.

**D3-b-2b-1b-2c — 씬 읽기 경로 소비자 이관 — ✅ 부분 완료 (2026-08-30). 래칫 15 → 12.**

`SceneManager`의 `DesirealizeGameObject` 3곳과 `ComponentFactory::LoadComponent`가
어댑터만 쓴다. `EntityAuthoringRead.cpp`는 **backend 등장 0**이 됐다.
`PromoteLegacyBone`·`PromoteLegacyTransform`도 옮겼다.

이를 위해 어댑터 오버로드를 셋 더했다: `Meta::Deserialize`(void*/템플릿 2종) ·
`NodeViewAccess::Make(const ReadNode&)` · `EntityAuthoring` 2함수.

★ **뷰의 표현은 하나로 유지했다.** `Make(const ReadNode&)`를 더할 때 뷰가 backend
노드와 어댑터 **둘 중 어느 것을 가리키는지 모르게** 될 뻔했다 — `Node()`가 그것을
구분할 수 없으므로 타입 편칭으로 조용히 망가지는 결함이 된다. 새 오버로드가 어댑터
안의 backend 노드를 가리키게 해서 표현을 하나로 두었다. 그 변환이 backend를 쓰는 것
자체가 "전환이 안 끝났다"는 표시이고, 래칫이 그것을 센다.

**★ Prefab 소환은 읽기 경로가 아니다 — 실측으로 갈렸다.**
`Prefab::InstantiateRecursive`는 정의를 `Clone`한 뒤 **트리를 변형**하고
(`UpgradeLegacyNavigation`이 `navigations`에 `parentHops`·자식 서수를 써 넣는다)
읽는다. 읽기 전용 어댑터로는 표현할 수 없는 **read-write 경로**이므로 D3-b-3(쓰기
경로)의 몫이다. 그때까지 `InferCreationType`·`PromoteLegacyTransform`에 backend를
받는 전환기 오버로드를 두었다 — **그 둘이 사라지는 것이 그 경로 전환의 완료 신호다.**

**남은 탈출구 12곳:** `ReflectionTypedYml.h` 6(변환 폴백 3 · `char` 1 · enum 1 ·
`NodeView` 브리지 1) · `AuthoringNodeViewAccess.h` 1(뷰 표현) · 훅 4
(`DataSystem`은 experiment 코덱이 backend 노드를 요구 — I5 소유) ·
`Prefab.cpp` 1(read-write 경로). **이들 대부분은 backend 교체와 함께 사라진다** —
지금 억지로 0으로 만들면 ryml에서 다시 써야 하는 코드를 두 번 쓰게 된다.

**검증:** Debug x64 CreatorEditor·Player exit 0, 게이트 9종 exit 0. 행동 변화 0.

**D3-b-2b-1b-3a — 어댑터를 이중 backend로 + 어댑터 파리티 게이트 — ✅ 완료
(2026-08-30).**

**★ 한 번에 다 못 바꾼다는 것이 실측으로 확정됐다.** 남은 탈출구 12곳 중 둘은
구조적으로 막혀 있다 — `DataSystem`은 experiment 코덱(I5 소유)에 yaml-cpp 노드를
넘기고, `Prefab` 소환은 트리를 변형하는 read-write 경로다. 그것들을 억지로 먼저
옮기면 ryml에서 다시 써야 하는 코드를 두 번 쓰게 된다.

그래서 `ReadNode`가 **두 backend를 모두 담는다.** 씬 로드처럼 준비된 원천부터
ryml로 파싱하고 나머지는 yaml-cpp로 남는다. 소비자는 어느 쪽인지 모른다 —
어댑터를 먼저 세운 값이 여기서 나온다. 분기 비용은 연산당 조건 하나이고, 파싱
비용(씬 로드의 60%)에 비하면 무시할 수 있다.

**★ 흡수해야 할 backend 비대칭이 셋이었다.**

| 축 | yaml-cpp | ryml |
|---|---|---|
| 맵의 키 | **진짜 노드** | **자식의 속성**(`key(id)`) |
| 널 | 노드 **타입** | "값이 있는데 `val_is_null`" |
| 없는 키 | 정의되지 않은 노드 | `operator[]`는 **abort** → `find_child` 필수 |

키 비대칭은 `Backend::RymlKey` 모드로 흡수했다(같은 id를 가리키되 `Scalar()`가
키를 돌려준다). `BackendNodeDuringTransition()`은 ryml 노드에서 **던진다** — 조용히
빈 노드를 주면 그 자리에서 데이터가 사라지고 아무도 모른다.

**★ 게이트 `verify-adapter-parity.ps1` — 앞선 두 파리티가 못 재는 축이다.**
파서 파리티는 **트리**를, 스칼라 파리티는 **값 변환**을 쟀다. 그러나 소비자가 실제로
부르는 것은 어댑터 연산 아홉 가지다. 같은 문서를 양쪽 backend로 어댑터에 넣어
전수 대조한다:

**파일 278 · 노드 15,339 · 맵 항목 13,814 · 차이 0.**

0을 세고 "차이 0"을 통과로 읽지 않도록 파일·노드·**맵 항목** 수를 함께 단정한다 —
맵을 한 번도 안 돌았다면 키 비대칭을 검사하지 않은 것이다.

**★ 변이가 정확히 390건을 붉혔다.** ryml의 `IsNull`을 `false`로 바꾸자
`IsNull yamlcpp=1 ryml=0`이 **390건** 나왔다 — D3-b-2b-0에서 코퍼스를 스캔해 센
`~` 개수와 **정확히 같다**. 검사의 이빨과 눈금이 함께 확인됐다.

**검증:** Debug x64 CreatorEditor exit 0, 어댑터 파리티 통과. `run-all.ps1` 편입.

**다음(D3-b-2b-1b-3b):** 씬 `LoadFile`을 `parse_in_arena`로 바꾸고 트리 수명을
로드 스코프에 건다. 그 순간 D3-b의 이득이 처음으로 **제품 경로 `SceneParse` A/B**에
나타난다 — 프로브 배율이 아니라 D0과 같은 profiler로 잰다.

**D3-b-2b-1b-3b — 훅 이관 + 뷰를 두 backend로 — ◐ 부분 완료 (2026-08-31). 래칫 12 → 10.
씬 전환은 I5 코덱 하나에 막혀 있다.**

`UIComponent`·`Animator`·`MeshRenderer`의 훅 본문을 어댑터로 옮겼다. 앞의 둘은
backend 등장 **0**이 됐다.

**★ 뷰가 씬 전환의 블로커였다.** `NodeView`는 backend 노드 하나를 가리키는 포인터
였는데, 두 backend의 표현이 다르다 — yaml-cpp는 노드 포인터로 족하지만 ryml은
`{트리, id}` 쌍이라야 한다. 뷰가 한쪽만 담을 수 있으면 씬을 ryml로 파싱하는 순간
`Make`가 ryml 어댑터에서 yaml-cpp 노드를 꺼내려다 던진다.

**불투명 두 워드 + 태그**로 바꿨다. 필드는 `const void*`와 정수뿐이라 **헤더는 여전히
포맷을 모르고**(§5 완료 기준 9), 의미 부여는 직렬화 계층만 한다. 태그가 핵심이다 —
두 표현을 섞어 놓고 구분하지 않으면 타입 편칭으로 조용히 망가진다.

★ 그 과정에서 표현을 하나로 강제하려다 `Make(const MetaYml::Node&)`를 지웠는데,
호출부가 루프 변수·중간 노드라 이름 있는 어댑터를 만들 수 없는 자리가 많았다.
**태그 방식이 옳았다** — 두 표현을 안전하게 공존시키면 호출부를 건드리지 않아도 된다.

**★ 남은 씬 전환 블로커는 하나다:**
`experiment::DeserializeMaterialPropertyValue(const YAML::Node&, ...)` —
`MeshRenderer::OnDeserialized`가 재질 override를 읽을 때 부른다. **I5 소유 코덱**이라
이 트랙이 옮길 수 없다. ryml 노드로 부르면 `BackendNodeDuringTransition()`이 던지므로,
씬을 ryml로 파싱하면 MeshRenderer가 있는 모든 씬이 실패한다.

★ 참고로 `DecodeMaterialReferenceNode`는 I5 소유가 아니라 `MeshRenderer.cpp`
익명 네임스페이스의 지역 함수였다 — 그래서 옮겼다. **"I5 것"과 "I5 것처럼 보이는 것"을
가르는 데 실측이 필요했다.**

**권고:** experiment 재질 코덱이 `Authoring::ReadNode`를 받도록 I5 트랙에 요청하거나,
D3-b-3(쓰기 경로)에서 그 코덱을 함께 옮긴다. 그 전에는 씬 ryml 파싱을 켤 수 없다.

**남은 탈출구 10곳:** `ReflectionTypedYml.h` 6 · `MeshRenderer.cpp` 1(위 블로커) ·
`DataSystem.cpp` 1(재질 경로) · `Prefab.cpp` 1(read-write 소환) ·
`AuthoringNodeViewAccess.h` 1(yaml-cpp 표현 생성).

**검증:** Debug x64 CreatorEditor·Player exit 0, 게이트 10종 exit 0. 행동 변화 0.

**D3-b-L — leaf 파서 정리 (신설 슬라이스) — ◐ 착수 (2026-08-31).**

★ **계획서에 구멍이 있었다.** D3-b의 판정은 "yaml-cpp consumer/include 0"인데,
하위 슬라이스(씬 읽기·쓰기·Document)는 그 경로만 덮는다. 실측 424곳의 분포는:

| 영역 | 등장 | 담당 |
|---|---:|---|
| experiment · cooked | **111** | **없음**(I5 소유) |
| 어댑터 · 리플렉션 | 105 | D3-b-3 · D3-b-4 |
| **독립 leaf 파서** | **~68** | **없었음 → 이 슬라이스** |
| 씬 · 프리팹 · CLI · 에디터 | ~140 | D3-b-2b-1b-3c · D3-b-3 |

**약 179곳(42%)에 담당 슬라이스가 없었다.** D3-b를 다 끝내도 yaml-cpp는 은퇴하지
못하고, "2일" 추정이 이 몫을 세지 않았다.

**leaf가 먼저인 이유:** 자기 파일만 읽고 평범한 데이터를 내놓아 **소비자가 backend에
묶여 있지 않다.** 씬 경로는 I5 코덱에 막혀 있지만 leaf는 지금 옮길 수 있고, 그래서
**제품 경로의 첫 ryml**이 된다 — 전환을 끝에서 검증하는 대신 여기서 먼저 밟는다.

`Authoring::ParsedDocument` 신설 — 파싱 결과가 트리를 소유한다. ryml 뷰는 트리를
소유하지 않으므로(§8) 홀더가 그 규칙을 타입으로 강제한다. `Authoring::Document`
(저작 문서 장기 소유)와 이름이 비슷하니 용도를 헤더에 적어 뒀다.

**옮긴 것:** `BlackBoard::LoadFromFile` · `TagManager::Load`. 두 파일의 잔존
yaml-cpp는 전부 **쓰기 경로**(Save)이며 D3-b-3 몫이다.

**★ 게이트가 여럿 초록이어도 그 경로가 지켜진다는 뜻이 아니다 — 변이로 확인했다.**
- `BlackBoard` 변이 → `verify-bt-smoke`가 잡았다("시퀀스 완주 로그가 없다").
- `TagManager` 변이 → **아무도 잡지 못했다.** scene 코퍼스·prefab·golden·play 왕복·
  asset-authoring-ownership 전부 초록이었다. 기존 `tag.authoring.probe`는
  Add/Has/Remove 즉 **메모리 조작만** 재고 디스크에서 읽은 결과를 보지 않았다.
  그래서 `list` 동작과 `verify-tag-authoring-read.ps1`을 만들었다(태그 21·레이어 16
  이름 집합 대조 — 개수만 세면 "다른 것을 같은 수만큼 읽은" 경우를 통과시킨다).

**★★ 그 변이 실험이 저작 자산을 파괴했다 — 그리고 몇 번의 실행이 지난 뒤에야
알아챘다.** 에디터는 종료 시 `Finalize()` → `Save()`를 무조건 부른다. 읽기가 깨지면
메모리 상태가 비어 있고, 그 빈 상태가 디스크를 덮어 **태그 21개가 통째로 사라졌다.**
`git checkout`으로 복구했다.

**이것은 실험 사고가 아니라 제품 결함이고, ryml 전환이 그 위험을 키웠다** — 두 파서의
수용 범위가 다르므로(21건) yaml-cpp가 읽던 파일을 ryml이 거부할 수 있고, 그 순간
저장이 손실을 확정한다. 두 곳을 고쳤다:
- `TagManager`에 `m_loadSucceeded`를 두고, **로드가 성공한 적 없고 파일이 존재하면
  Save를 거부**한다. 빈 상태 저장은 사용자가 정말 전부 지운 경우에만 정당하다.
- 게이트가 자산을 스냅샷 뜨고 **바이트 단위로 복원·대조**한다.

가드는 변이로 검증했다 — 파싱을 실패시켜도 **자산이 보존됐고**(바이트 동일) 게이트는
빨갛게 실패했다.

---

**ShaderMeta 27곳 이식 — 완료 (2026-08-31).** 전부 읽기 경로였다.

**먼저 자를 세웠다.** 규칙대로 착수 전에 변이로 확인했더니, 이 파서의 계약은
`dx12.selftest`(`EnhancedSceneRendererSelfTest::ValidateShaderMeta`) **안에만** 있었다.
그리고 그것은 회귀 세트(run-all)에 없고, 자기 하네스인
`Tools/dx12-validation/Invoke-DX12Validation.ps1`은 vcpkg baseline preflight에 막혀
이 기계에서 돌지 않는다. 변이(`if (false && !known)` — unknown-field 거부 무력화)를
넣고 다시 빌드한 결과:

| 하네스 | 결과 |
|---|---|
| `dx12.selftest` | 실패(잡는다) — 하지만 세트에 없고 하네스는 preflight에 막힘 |
| `verify-experiment-asset-cooker` | **통과(눈멀다)** |

즉 **정기적으로 도는 게이트 중 이 경로를 지키는 것이 없었다.** TagManager에서 순서를
놓쳐 저작 자산을 잃었으므로, 이번에는 이식 **전에** `shadermeta.probe`와
`verify-shadermeta-authoring-read.ps1`을 만들고 run-all에 넣었다.

**두 방향을 잰다.** 수용(실자산 6개의 property·axis·pass **이름 집합**을 자산 텍스트에서
유도해 대조)과 거절(사유별 6종). 저작 코퍼스는 전부 유효하므로 수용만 재면 "무엇이든
통과시키는 파서"가 만점을 받는다 — backend 교체에서 가장 흔한 실패가 그 방향이다.

거절 사례에는 **ryml 고유 위험**을 넣었다:
- `missing-source` — ryml `operator[]`는 없는 키에서 **abort**한다. 어댑터의
  `find_child` 흡수가 맞는지 상시로 밟는다.
- `numeric-bool`(`depthWrite: 1`) — YAML 1.1 bool 표. 스칼라 파리티(D3-b-2b-0)가 두
  backend에서 갈리는 것으로 실측한 부류다.
- `sequence-root` — 맵이 아닌 루트에 맵 연산.

**변이로 이빨을 증명했다**(첫 실행부터 초록이었으므로 필수였다):

| 변이 | 게이트 반응 |
|---|---|
| unknown-field 거부 무력화 | `rejected=5/6`, `unknown-field accepted=1`을 지목 |
| 마지막 property 하나 흘림 | 6개 파일 각각에서 **빠진 이름을 지목**(`emissiveMap`·`albedoMap`·`alphaCutoff`) |

**이식 결과.** `YAML::Load` → `Authoring::ParsedDocument::ParseText`,
`ValidateMap`의 키 순회 → `MapEntry`(ryml에서 키는 자식의 **속성**이지 노드가 아니다),
`node[index]` → `At()`, `as<T>()` → `As<T>()`, `Scalar()`가 `string_view`가 되어
문자열 대입부는 `AsString()`. `catch (YAML::Exception)`은 사라졌다 — ryml은 예외가
아니라 abort가 기본값이라 잡을 것이 없고, 파싱 실패는 `ParsedDocument`가 값으로
돌려준다. **탈출구 0개**(래칫 10/10 불변).

검증: 새 게이트 `files=6 parsed=6 rejectCases=6 rejected=6`, `dx12.selftest` 통과,
`verify-experiment-asset-cooker`·`verify-material-authoring-corpus`·
`verify-asset-guid-contract` 통과.

---

**부수 수정 — `verify-player-runtime-hygiene`(D1)이 주석을 참조로 세고 있었다.**
`PrefabUtility.cpp`의 "efsw 워처가 이 필드를 바꾼다"라는 **설명 한 줄** 때문에 이
검사가 빨갰다. 재는 것은 링크되는 참조인데 원문을 그대로 grep했다. 블록·줄 주석을
걷어낸 뒤의 본문만 보도록 고쳤고, 변이(엔진 트리에 실제 `#include <efsw/efsw.hpp>`)로
**여전히 실코드를 잡는 것**을 확인했다.

---

**★ 이 세트에는 내 트랙이 아닌 실패가 하나 남아 있다.** `HierarchyStore 읽기 경계`가
7건으로 빨간데 전부 I5 트랙 소유다 — `ExperimentModelMigration.cpp` 6건(역브리지가
`m_parentIndex`를 직접 쓴다)과 `ConsoleCommandSystem.cpp:3025` 1건. **HEAD에서 이미
빨갛고**(해당 줄이 HEAD에 그대로 있고 `ExperimentModelMigration.cpp`는 워킹트리
수정본이 아니다) 이 슬라이스와 무관하다. 계층 단일 정본 경계를 어떻게 지킬지는 I5의
설계 결정이므로 여기서 코드를 고치거나 그쪽 게이트를 느슨하게 하지 않았다.

---

**남은 leaf(~35곳):** `EditorSettingsStore` 13 · `RuntimeSettings` 4 ·
`FoliageComponent` 4 · `Model` 4 · `VolumeComponent`·`PhysicsManager`·
`BehaviorTreeComponent` 각 2 · `Terrain`·`ModelLoader` 각 1.
**착수 전에 각각 어떤 게이트가 지키는지 변이로 확인할 것** — TagManager가 그 필요를
증명했고, ShaderMeta는 "강한 계약이 있어도 도는 세트에 없으면 없는 것과 같다"를
덧붙였다.

- **D3-b-3:** 쓰기 경로(`Meta::Serialize`·`YAML::Emitter`). 파싱 이득은 없고 저작
  왕복 정확성이 판정이다.
- **D3-b-4:** `Document::Impl` 교체 + `DocumentAccess`/`NodeViewAccess` 반환 타입.
  위 둘이 끝나야 변환 없이 바뀐다.

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

**D5-b2c — texture/ShaderMeta/scene/prefab producer — ✅ 완료 (2026-08-29).** 이 종류의 Derived artifact와
material의 shader/texture dependency entry를 완성해야 D5-b 전체가 닫힌다. legacy cooked cache
호환은 두지 않고 전수 재쿠킹한다. 따라서 제품 Cook/pak 배선은 아직 완료가 아니다.

분할한다. 하나로 묶으면 어느 producer가 폐포를 깼는지 못 가른다.

| 슬라이스 | 내용 | 상태 |
|---|---|---|
| **b2c-1** | texture producer + manifest entry | ✅ 2026-08-29 |
| **b2c-2** | ShaderMeta producer + shader/texture dependency | ✅ 2026-08-29 |
| **b2c-3** | material dependency 배선 + standalone material 2 | ✅ 2026-08-29 |
| **b2c-4** | scene 14 · prefab 9 producer | ◐ prefab 9 ✅ · scene 14 차단 |
| **b2c-5** | 전체 GUID 폐포 fail-closed + AssetPacker/pak 게시 | ✅ 2026-08-29 |

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

**D5-b2c-2 — ShaderMeta producer — ✅ 구현·전수 검증 완료 (2026-08-29).**
`ShaderMetaCookProducer`가 `.shadermeta`를 `Derived/ShaderMeta/<앞2자리>/<guid>.shadermeta`
artifact와 `kind=ShaderMeta` entry로 만든다. `AssetCooker --shadermeta`가 model/texture와
같은 staging/원자 게시 경로를 탄다. artifact는 texture와 같은 pass-through이고,
`formatVersion`은 **`ShaderMeta::kSchemaVersion`에서 유도한다** — 손으로 올리는 숫자가
아니라 schema 정본에서 나오므로 schema가 2가 되는 날 구버전 artifact가 자동 거부된다.

**검증은 `ShaderMetaLoader::Parse`가 한다. 두 번째 파서를 만들지 않았다.** 이를 위해
`ShaderMeta.cpp`를 RenderEngine 유니티 빌드에서 분리했다(cooked TU와 같은 처리) — 유니티
object에 묶여 있으면 도구 링크가 렌더러 전체를 끌어온다. 분리 후 AssetCooker는 yaml-cpp만
추가로 끌고 깨끗하게 링크된다.

★ **HLSL source는 manifest dependency로 넣지 않는다.** `source:`가 가리키는 `.hlsl`은
GUID와 `.meta`를 갖지만 Derived artifact가 아니다 — B2가 그것을 **content**로 pak에 싣고
경로로 주소를 매긴다(`build.ps1`이 "B3 전까지 shader는 source HLSL을 pak에 포함한다"고 직접
적는다). 여기서 `Derived/Shaders/...`를 만들면 셰이더 artifact 소유자가 둘이 된다. 대신
**해소 가능한지는 증명한다** — source의 sidecar GUID를 읽어 canonical UUIDv4임을 확인하고,
그 GUID를 도구 요약에 찍는다(아무도 안 읽는 필드로 두지 않는다). 간선을 그릴 주체는 B3다.

실측: `.shadermeta` 6개 전수 cook, 6,647 B, entry 6. 각 항목의 name/source/sourceGuid/
property·keyword·pass 수를 요약에 남긴다(EnhancedForward property 11 등).

★ **이 슬라이스가 게이트의 결함을 먼저 잡았다.** 합성 55·실자산 7이 첫 실행부터 통과해
변이 4종을 돌렸더니 **둘이 그대로 살아남았다.** 원인은 `ExpectRejected`가 "거부됐다"만 보고
**어느 guard가 걸었는지**는 안 봤다는 것이다 — source 존재 검사를 지워도 그 다음 `.meta`
판독이 거부했고, schema 검사를 무력화해도 빈 `source`가 경로 해소에서 거부됐다. 기대하는
issue context를 함께 단정하도록 고치자 곧바로 **내가 넣은 guard 두 개가 한 번도 도달하지
않는 죽은 코드**임이 드러났다: `Parse`가 `IsSafeRelativeSource`로 `..`·절대경로를 막고
`is_regular_file`로 존재를 확인한 뒤에야 성공을 돌려준다. 두 guard를 삭제했고, 같은 결함이
있을 이유가 없어 **texture 게이트도 함께 강화했다**(단정 78 → 88, 전부 통과 — texture
producer 쪽에는 죽은 guard가 없었다).

강화 후 변이 5종이 전부 정확히 빨개졌다: 정본 파서 결과 무시 4건, source sidecar 판독
제거 3건(+정상 경로 2건), artifact 바이트 상수화 3건, 확장자 guard 제거 3건, 해소 불가능한
dependency 추가 1건. 게이트 `experiment.smcook`(합성 66·실자산 7)을 신설했고 experiment
게이트 14회 호출이 전수 통과했다(실패 0).

★ 남은 정직한 구멍: `formatVersion`이 schema에서 **유도**된다는 것은 지금 게이트로 가릴 수
없다. `kSchemaVersion`이 1인 동안 상수 `1`과 구별되지 않기 때문이다. schema가 2로 오르는
날 이 단정이 자동으로 판별력을 얻는다.

**D5-b2c-3 — material dependency 배선 + standalone material — ✅ 구현·전수 검증 완료
(2026-08-29).** D5-b2a는 "shader/texture producer entry가 생기기 전에는 해소 불가능한
dependency를 쓰지 않는다"며 재질 의존을 비워 뒀다. b2c-1/b2c-2가 그 전제를 없앴으므로 이제
재질 entry가 `shaderAssetId` + 참조 texture GUID를 든다(같은 texture를 가리키는 슬롯은 접고,
nil은 간선이 아니다). standalone `.asset` material 2개는 `MaterialCookProducer`가
`Derived/Materials/<앞2자리>/<guid>.asset`으로 굽는다.

★ **가장 큰 발견은 임베디드 texture가 어디에도 없었다는 것이다.** 실측상 texture 참조
100개 중 **96개가 임베디드**이고, CEMC는 texture 바이트를 싣지 않는다(`TextureReference`는
ID와 진단 경로만 든다). 즉 b2c-1이 디스크 texture만 다루는 동안 지배적인 경우가 통째로
빠져 있었고, 재질 의존을 그리는 순간 전부 해소 불가능한 GUID가 됐을 것이다.
`resolveTextureAsset` 콜백이 IR의 `ImportedTexture`를 보는 **유일한 지점**이라 거기서
바이트를 붙잡아 Derived artifact로 뽑는다. 실측 **25,473,365 B**가 이전에는 버려지고 있었다.

★ 확장자는 **매직 바이트로 판별한다.** `ImportedTexture::mimeType`은 존재하지만 **아무도
채우지 않는 죽은 필드**다(glTF 임포터가 sourceKey·name·colorSpace·embeddedBytes만 설정한다).
비어 있는 필드를 믿으면 임베디드가 전부 "확장자 불명"이 된다. allowlist에 `.jpg`를 더했고
게이트가 네 컨테이너를 모두 태운다 — 다만 **실자산 임베디드는 전부 png라 `.jpg`도 `.dds`도
실자산 커버리지가 0**이고 합성 게이트가 유일한 증거다.

★ **`std::move`로 비워진 entry를 나중에 읽는 잠복 결함이 드러났다.** `AssetCooker`가
`manifest.entries.push_back(std::move(entry))`로 product의 entry를 옮겨 놓고, 게시 전
검증에서 같은 vector를 다시 읽었다. 그동안은 `assetId`만 읽었고 `AssetId`는 옮겨도 값이
남아 아무 일이 없었는데, kind·artifactPath까지 읽자 곧바로 빈 경로로 터졌다. 복사로 바꿨다.
게시 전 검증 루프의 "index 1부터는 전부 material"이라는 전제도 함께 고쳤다 — 이제 그
목록에는 model·embedded texture·material이 섞인다.

★ **게이트가 producer의 구멍을 하나 더 잡았다.** brace 표기 texture GUID를 검사에 넣었더니
YAML이 그것을 **flow mapping**으로 읽었고, "스칼라가 아니면 건너뛴다"는 코드가 간선을 소리
없이 없앴다 — 잘못 적힌 GUID가 "텍스처 없음"과 같은 모습이 되는, 폐포가 원리적으로 못 잡는
형태다. 키가 있는데 스칼라가 아니면 실패하도록 고쳤다.

★ **변이 하나가 살아남아 게이트를 보강했다.** 모델 재질에서 texture 간선을 통째로 빼도
`texture 간선 0`으로 통과했다 — 단정들이 "그린 간선이 해소되는가"만 봤기 때문이다. 뽑아낸
임베디드가 모두 어떤 재질의 의존인지(반대 방향)를 더하자 정확히 3건이 빨개졌다.

실측: 전수 cook에서 model 14 · material 52 · 임베디드 texture 96 · 외부 texture 112 ·
ShaderMeta 6 · standalone material 2 = **manifest entry 282**, 두 번 cook한 tree hash 동등,
CEMF 42,476 B. **manifest writer가 이미 폐포를 강제한다** — 모델만 구우면 재질의 ShaderMeta
의존이 해소되지 않아 실패하고, 넷을 함께 구워야 통과한다(b2c-5의 전제가 이미 서 있다).
게이트 `experiment.matcook`(합성 70 · material 5 · model 15)을 신설했고 변이 6종이 전부
정확히 빨개진다. experiment 게이트 15회 호출 전수 통과.

**D5-b2c-4 — scene/prefab producer — ◐ prefab 9 완료 · scene 14 차단 (2026-08-29).**
`SceneCookProducer`가 `.creator`/`.prefab`을 확장자로 갈라 `Derived/Scenes/`·
`Derived/Prefabs/` artifact와 Scene/Prefab entry로 만든다. 문서를 재귀 순회해
`m_fileGuid`·`m_prefabFileGuid`·`m_textureGuid`를 간선으로 뽑고, nil·중복·**자기 참조**는
접는다. 프리팹은 자기 루트에 자기 GUID를 `m_prefabFileGuid`로 적어 두는데(인스턴스 표기)
그대로 간선을 그리면 manifest가 self-dependency로 거부한다 — 실제로 처음 실행에서 9개가 전부
그렇게 터졌다.

★ **씬 14개는 굽지 못한다. `.meta`가 하나도 없다.**
`verify-asset-guid-contract.ps1`의 sidecar 대상 확장자 목록에 **`.creator`가 없고**(`.prefab`은
있다), 그래서 씬은 asset identity 자체를 갖지 않는다. 씬은 지금 **경로로** 참조된다
(`GameBuilderSystem`이 시작 씬을 `.creator` 경로로 받는다). **D5-c의 "Player가 `.meta`나
source path 탐색 없이 scene을 해석한다"는 이 상태에서 성립할 수 없다** — 조회할 GUID가 없다.
쿠커가 씬 GUID를 지어내지 않는다. §3.4 정책에 `.creator`를 편입하고 sidecar를 발급하는
**authoring 작업**이 선행이며, 이는 brace 표기 정규화와 같은 묶음이다.

★ **텍스처는 아직 GUID로 참조되지 않는다(실측 17건).** 씬의 인라인 재질에는
`m_propertyValues`/`m_textureGuid`가 **0건**이고 legacy `m_baseColorTexName` 같은 파일명
필드만 있다. 런타임은 GUID 우선·이름 폴백(`DataSystem::FinalizeMaterialRuntime`)이라 지금은
폴백이 그것을 나른다. 없는 GUID를 이름에서 지어내면 §3.6.1이 죽이려는 평탄화(stem 충돌 17건)를
쿠킹 안으로 다시 들여오는 것이므로, **간선으로 그리지 않고 센다.** 도구가
`legacyTextureNameRefs`로 찍고, 그 수가 0이 되어야 D5-c가 성립한다.

★ 같은 이유로 BT/blackboard GUID(2건)도 센다 — producer가 없어 간선을 그리면 해소되지 않는다.

★ **`m_fileGuid`는 재질 GUID가 아니라 모델 GUID다.** 인라인 `m_Material` 안에 있어 재질 것처럼
보이지만 `MeshRenderer`가 그것을 `LoadModelGUID`에 넘긴다(전수 10개가 모두 `Models/*.glb`로
해소된다). 메시는 그 모델 **안에서 이름으로** 고르므로 서브에셋 조회이고, 주소 단위는 모델
artifact가 맞다. 처음에 "메시가 이름 참조"라고 적었다가 로더를 읽고 정정했다.

★ **변이 하나가 살아남아 진단을 갈랐다.** 비스칼라 참조 guard를 지워도 게이트가 초록이었다 —
비스칼라 노드의 `Scalar()`가 빈 문자열을 돌려줘 그다음 파싱이 대신 거부했기 때문이다. 거부
자체는 맞지만 진단이 달라진다(brace 표기는 저작자가 실제로 저지르는 실수라 메시지가 중요하다).
그 guard에 `scene.reference.kind` 사유를 따로 주어 구분되게 했다.

실측: prefab 9 전수 cook 15,015 B, manifest entry **291**(b2c-3의 282 + 9),
`legacyTextureNameRefs=0`·`unproducedGuidRefs=2`. 게이트 `experiment.scenecook`
(합성 59 · 실자산 4)을 신설했고 변이 6종이 전부 정확히 빨개진다. Scene kind와
`m_textureGuid` 간선은 **합성 게이트가 유일한 커버리지**다(실자산으로는 못 태운다).
experiment 게이트 16회 호출 전수 통과.

**D5-b2c 정리 묶음 — sidecar 표기 정규화 · `.creator` identity 편입 · guid 게이트 강화 —
✅ 완료 (2026-08-29).** b2c-1~b2c-4가 찾아낸 저작 데이터·게이트 결함 셋을 한 번에 닫았다.
**Cook은 데이터를 고치지 않는다** — 새 저작 경계 도구
`Tools/migration/Repair-AssetSidecarIdentities.ps1`(dry-run 기본, `-Apply` 필요)이 맡는다.

1. **표기 정규화 7건.** `{...}` brace 표기 sidecar를 canonical 소문자로 다시 썼다. **값은
   보존한다** — 표기만 바꾼다. 벗겨도 UUIDv4가 아니면 손대지 않고 `refused`로 보고한다(값을
   지어내는 것은 정규화가 아니라 재발급이고 사람이 결정할 일이다). 원본 줄바꿈 관습을 지킨다.
2. **`.creator` identity 편입 14건.** 씬은 sidecar 대상 확장자 목록에 없어 GUID 자체가 없었다.
   `EditorAssetDatabase::m_registeredFiles`와 게이트 목록에 **함께** 추가하고 기존 14개를
   백필했다 — 한쪽만 고치면 백필이 되거나 신규가 되거나 둘 중 하나만 산다.
3. **guid 게이트 강화.** 최상위 guid를 subasset과 같은 canonical 규칙으로 검사한다.

★ **강화 직후 변이가 절반만 잡혔다.** brace 변이는 빨개졌는데 **대문자 변이가 그대로
통과했다** — PowerShell의 `-notmatch`가 **기본이 대소문자 무시**라 `[0-9a-f]`로 써 놓아도
대문자가 지나간다. 즉 `$canonicalV4Pattern`은 이름만 canonical이었고 **소문자를 강제한 적이
없다.** subasset 검사도 같은 연산자를 쓰고 있었으므로 그쪽도 함께 뚫려 있었다. 둘 다
`-cnotmatch`로 고쳤고, brace·대문자·subasset 대문자 변이 3종이 정확히 빨개진다.

실측: `meta=226 → 240`, `invalid=0`, `invalidSubasset=0`, `uuidV4=240`, `nonV4=0`,
`duplicateGroups=0`, `d2Ready=true`. **전 corpus 전수 cook이 처음으로 닫혔다** —
model 14 · material 52 · 임베디드 texture 96 · 외부 texture **119**(112 → 119) ·
ShaderMeta 6 · standalone material 2 · **scene 14** · prefab 9 = **manifest entry 312**,
CEMF 47,232 B, 두 번 cook한 tree hash 동등, stderr 0. 실자산 씬 게이트가 모델 간선 8개와
legacy 이름 참조 8건을 실제로 뽑는다. experiment 게이트 17회 호출 전수 통과.

★ 남은 것은 `legacyTextureNameRefs=17` 하나다. 이건 표기 문제가 아니라 **저작 데이터
이주**(씬 인라인 재질에 `m_propertyValues`/`m_textureGuid` 채우기)이고 D5-c의 마지막 선행이다.

**D5-b2c-5 — 전체 폐포 fail-closed + pak 게시 — ✅ 구현·실제 패키지 검증 완료
(2026-08-29). D5-b2c 전체가 닫혔다.**

**쿠커의 최종 폐포 스윕.** 그전까지의 검증은 전부 **product 에서 출발**했다 — "내가 만든 것이
manifest 에 있는가". 그 방향만으로는 둘을 못 본다: manifest 가 이름 붙였는데 **디스크에 없는**
artifact, 그리고 디스크에 있는데 **manifest 가 모르는** orphan. 그래서 게시 직전에 manifest 를
정본으로 반대 방향을 훑는다 — (1) 모든 dependency 가 판독본에서 해소되는가, (2) 이름 붙인
artifact 가 전부 실재하며 크기·해시가 맞는가(stale), (3) staging tree 에 manifest 가 모르는
파일이 없는가, (4) 파일 수 = **서로 다른 artifact 경로 수** + manifest 1(material 처럼 model
artifact 를 공유하는 subasset 이 있으므로 entry 수가 아니다).

변이 4종이 각각 정확히 걸린다: orphan 파일 주입, artifact 삭제, 내용 변조(stale), 판독본에서
**참조되는** entry 제거. 마지막은 처음에 참조 없는 텍스처를 지웠더니 orphan 검사가 먼저 잡아
dependency 재확인을 못 봤다 — 참조되는 entry 로 바꾸자 그 검사가 살아 있음이 확인됐다.

**build.ps1 통합.** `Invoke-ModelCook`/`Assert-ModelCookOutput`을 다섯 종 전체로 일반화했다
(`Invoke-AssetCook`/`Assert-CookOutput`). 종류별 GUID-addressed 경로 규약을 쿠커와 **독립적으로**
정규식으로 다시 확인하고(`-cnotmatch` — guid 게이트가 빠졌던 대소문자 함정을 여기서도 피한다),
파일 수 기대값은 다시 유도하지 않고 **쿠커가 폐포 스윕에서 실제로 센 `artifactPaths`** 를 읽어
맞춘다. 같은 규칙을 두 곳에서 유도하면 둘 다 틀렸을 때 서로를 확인해 주지 못한다.

실측(Debug/Project/SkipVerify 실제 패키징):

| | |
|---|---|
| source | model 14 · texture 119 · shadermeta 6 · material 2 · scene+prefab 23 |
| Derived | Models 14 · Textures **215** · ShaderMeta 6 · Materials 2 · Scenes 14 · Prefabs 9 |
| 합계 | artifact 260 + manifest 1 = **261 파일**, 462,874,538 B, CEMF 47,232 B |
| pak | **789 entry**(D5-b2b2 529 → 789), 그중 Derived 261 전부 |

Textures 215 = 외부 119 + 임베디드 96. manifest entry 312 중 260 경로 — 차이 52는 model
artifact 를 공유하는 재질 subasset 이다.

★ **드러난 것을 숨기지 않는다.** 빌드가 둘을 노란색으로 보고한다.
`legacyTextureNameRefs=17`(D5-c 선행)과 `legacyModelCookCaches=14` — 후자는 `InputMode Project`
가 gitignore 된 `Assets/Models/*.asset` legacy 쿠킹 캐시까지 복사해 **파생물이 콘텐츠 서브트리
안에 실려 나가는** §3.6.1 그 형태다. 처음에는 이걸 throw 로 막았다가 되돌렸다 — legacy 로더가
아직 그 캐시를 읽으므로 여기서 패키지 구성을 바꾸는 것은 b2c-5 의 범위 밖이고(끊는 것은 I5/I6),
material 열거에서 제외하고 세어서 보고만 한다. 둘 다 `package-manifest.json` 의 `cook` 섹션에
기록된다.

**D5-b2c 후속 — 씬 텍스처 GUID 이주 — ✅ 완료 (2026-08-29). `legacyTextureNameRefs=17 → 0`.**

★ **먼저 계수 기준이 틀렸다는 것이 드러났다.** b2c-4는 "legacy 이름 필드가 비어 있지 않으면"
셌는데, `DataSystem::SynchronizeLegacyMaterialProperties`가 **두 방향을 모두 채운다** — 이름에서
GUID를 해석해 `m_propertyValues` 항목을 만들고, **GUID에서 이름도 되채운다.** 그래서 이주가
끝난 재질도 이름 필드를 계속 갖고, 옛 기준으로는 **이주해도 숫자가 줄지 않는다.** 위험한 것은
이름 필드의 존재가 아니라 *GUID가 없어서 이름에 의존하는 것*이므로, 같은 인라인 재질에 대응
`m_textureGuid`가 있으면 세지 않도록 고쳤다. 변이(GUID 유무를 무시)가 정확히 1건을 빨갛게 한다.

★ **새 변환기를 만들지 않았다.** 이주 로직은 이미 위 함수에 있으므로 정본 경로를 태운다 —
`Tools/migration/Invoke-SceneTextureGuidMigration.ps1`이 에디터 CLI(`scene.switch` → `scene.save`)를
돌리고 결과를 검증할 뿐이다. 대상 3개(FT_Primitives 8 · Test1 6 · Test2 3 = 17)를 사전에 셌고,
17건 전부 파일명이 **유일하게** 해소됨을 먼저 확인했다(모호 0).

★ **부수 효과가 컸다. 예고 없이 일어났다.** 로드-저장 왕복이 Test1/Test2를 **구형 씬 스키마에서
신형으로 재작성했다**(`m_SceneObjects`→`m_Entities`, `- GameObject:`→`- Entity:`, `m_transform:`→
Transform 컴포넌트). 부채를 줄이는 방향이지만 텍스처 GUID만 넣으려던 범위를 넘는다. 검증:
`scene.dump` 오브젝트 수가 이주 전후 동일(Test1 68·Test2 63·FT_Primitives 11)하고, 구조 비교에서
손실 항목이 없다(FT_Primitives는 `m_cameraIndex`→`m_isPrimary` 스키마 이주 1건뿐).

★ **내가 한 번 잘못 단정했다.** diff의 줄 이동을 삭제로 오독해 "이주가 `PackageSmokeProbe`
ScriptComponent를 지웠다"고 말했다. 패키지 입력 스냅샷의 이주 전 원본과 대조하니 ScriptComponent
수가 전후 동일했다(2/1). 손실은 없었다.

★ **에디터의 `.creator` sidecar 자동 발급이 실증됐다.** 검증용 임시 씬 3개를 Scenes 폴더에 두자
에디터가 `.meta`를 발급했고, guid 게이트가 `missingTarget=3`으로 잡았다. 정리 후 `d2Ready=true`
복귀. 등록이 실제로 동작한다는 증거이자, 임시 파일도 받는다는 함정이다.

실측: 전수 cook `legacyTextureNameRefs=0`, 씬 게이트 실자산이 `간선 16(model 8·texture 8)·
legacy이름 0`을 뽑는다(이주 전 `간선 8(model 8)·legacy이름 8`). experiment 게이트 17회 전수
통과, 실제 패키지 빌드 성공(entry 789, Derived 261). **씬 파일은 gitignore 대상이라 커밋되지
않는다** — 다른 워크스페이스는 위 스크립트를 각자 돌려야 한다.

**D5-c-1 — `CookedThenSource` resolver — ✅ 구현·검증 완료 (2026-08-29).**
★ **경계 합의: D5-c는 resolver까지다.** 렌더 경로가 `experiment::Model`을 직접 소비하는 것은
I5이고 이번에 넘지 않는다.

`ResolvingModelDecoder`가 cooked/source decoder 둘을 들고 `ModelSourcePreference`대로 순서를
정한다. **그전까지 `CookedThenSource`는 이름만 있었다** — `ModelLoader`가
`unique_ptr<IModelDecoder>` 하나만 받아서 `SourceOnly`·`CookedOnly`는 전용 decoder를 꽂아 검사할
수 있었지만 **cooked를 시도하고 거부되면 source로 넘어가는 경로는 만들 수가 없었다**(§1.2).

★ **폴백은 관측 가능해야 한다.** cooked가 늘 거부되는데 조용히 source로 도는 상태는 "느리지만
동작하는" 모습이라 아무도 알아채지 못한다. legacy가 `Assets/Models/` 밖 모델에서 캐시를 두고도
매번 Assimp를 돌던 것이 정확히 그 형태였다(§3.6.1 ★). 그래서 폴백 시 `CookedFallbackToSource`
Info를 **정확히 한 줄** 남기고, cooked의 거부 사유도 지우지 않고 함께 보고한다. cooked 거부는
Error로 승격하지 않는다 — 포맷 버전 불일치는 정상적인 재임포트 신호다. 다만 `CookedOnly`는
폴백할 곳이 없으므로 그대로 실패다.

★ **decoder 부재는 조용히 넘어가지 않는다**(`MissingPreferredDecoder`). preference를 무시하고
다른 쪽으로 새면 호출자가 고른 정책을 뒤집는 것이다.

게이트 `experiment.resolver`(합성 34)를 신설했다. 자산을 읽지 않고 가짜 decoder 둘을 꽂아
**호출 계수**로 본다 — "결과가 맞다"만 보면 안 불려야 할 decoder가 불려도 우연히 같을 수 있다.
변이 5종이 정확히 걸린다: 폴백 기록 제거 1건, cooked issue 폐기 1건, `SourceOnly` 무시 3건,
`CookedOnly` 누수 4건, decoder 부재 침묵 2건. experiment 게이트 18회 호출 전수 통과.

**D5-c-2 — `CookedAssetCatalog` + 전수 해석 증명 — ✅ 구현·검증 완료 (2026-08-29).**
CEMF 하나를 읽어 **GUID로 묻는** 런타임 경계. 조회(`Find`)·artifact 실경로 해석
(`ResolveArtifactPath`)·kind별 계수, 그리고 **폐포 질의**(`CollectClosure`)를 준다. 폐포는
**위상 순서**다 — 의존이 먼저 오고 root가 마지막이라, 로더가 그 순서대로 열면 참조가 항상 이미
준비돼 있다. 순환(중첩 프리팹이 서로를 품는 저작 오류)은 방문 집합으로 자연히 멈춘다.

★ **`AssetMetaRegistry`를 대체하지 않는다.** 그쪽은 GUID→**원본** 경로, 이쪽은 GUID→**artifact**
경로다. 하나로 합치는 것은 legacy 로더가 은퇴하는 I5/I6의 일이고, 지금 합치면 어느 쪽이 정본인지
흐려진다. 대체 대상은 `DataSystem::LoadAssetCatalog`의 **`.meta` 재귀 스캔**이며 그 배선은 I5에서
붙는다 — **지금 이 표의 소비자는 게이트 하나이고, 그 사실을 숨기지 않는다.**

★ **전수 해석 증명.** 게이트가 실제 Derived tree의 CEMF로 catalog를 세우고 **모든 GUID**에 대해
(1) 조회 (2) artifact 실재 + 크기·해시 일치 (3) dependency 해소 (4) 폐포 위상 순서를 확인한다.
쿠커의 폐포 스윕은 **게시 직전 staging**을 보고, 이것은 **게시된 tree**를 소비자 관점에서 다시
본다 — 같은 성질을 다른 쪽에서 재므로 둘이 갈라지면 드러난다.

실측: `entry 312 · 파일 260 · 간선 235 · 폐포합 687 · 최대폐포 96`
(model 14 · material 54 · texture 215 · shadermeta 6 · scene 14 · prefab 9).
**최대폐포 96** = 자산 하나를 로드하는 데 필요한 최대 개수다.

★ **변이 2종이 살아남아 둘을 갈랐다.**
- **미해소 dependency 검사는 도달 불가다.** `ReadAssetManifest`가 `ValidateManifest`로 이미
  폐포를 검사하므로 catalog에는 닫힌 manifest만 들어온다. 그 분기를 `continue`로 바꿔도
  게이트가 초록이다 — 태울 데이터가 없다. **삭제하지 않고 남긴 이유는 UB 때문이다**(아래에서
  `entry->dependencies`를 역참조한다). "혹시 모르니"가 아니라 도달 불가를 확인한 뒤 남겼고,
  그 근거를 코드에 적었다.
- **root 미존재 검사는 죽어 있었다.** 지워도 아래 dependency 검사가 같은 입력을 거부해서
  "실패했다"만으로는 초록이었다. root를 물었는데 "dependency"라고 답하면 호출자가 엉뚱한 곳을
  본다 — 게이트가 **실패 사유 문구**까지 보게 하자 정확히 잡힌다(b2c-4의 `scene.reference.kind`와
  같은 처방).

나머지 변이 3종은 정확했다: 전위 순회(위상 순서 붕괴) 9건, derived root 무시 4건, 판독 실패 시
부분 catalog 게시 34건. 게이트 `experiment.catalog`(합성 31 · 전수 11), experiment 게이트 19회
호출 전수 통과.

**D5-c 잔여 — Player 배선. ⏳ I5-M5 S1~S4 대기(08-30 확인).**
★ 위 catalog를 Player 기동 경로에 꽂아 `.meta` 재귀 스캔을 대체하는 일이 남았다.
대체 대상은 `DataSystem::LoadAssetCatalog`(`DataSystem.cpp:210`)이고, 현재 Player는
`PlayerMain.cpp:139`에서 이를 호출해 부팅마다 `.meta` 240개를 전수 파싱한다(§1.7 ②). 다만 그 순간
`LoadModelGUID`가 catalog를 소비하게 되고, 그것은 **I5(렌더 경로 치환)와 소유권이 겹치는
지점**이다 — 이번 합의(“경계는 resolver까지”)에 따라 넘지 않았다.
pak의 CEMF를 기동 시 읽어 GUID→artifact catalog를 세우고, 지금의 `.meta` 디렉터리 재귀 스캔
(`DataSystem::LoadAssetCatalog`)을 대체한다. Player가 `.meta`나 source
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

**08-30 잔여 재산정.** D2 전체와 D5-a~D5-c-2가 닫힌 현재, 남은 것은
D0 · D1(축소) · D3-a(축소) · D3-b · D4 · D5-c 잔여 · D5-d · D6 · D7(선택)이다.
D2·D5가 D0보다 먼저 진행된 탓에 지금 **실제 하드 블로커는 D0 하나**였다:

- 회귀 세트 54종 중 **직렬화 성능 게이트는 0개**였고, §5 완료 기준 2("D0 기준선 대비
  수치 개선")와 D5-d("Player 씬 전환을 D0과 대조")가 모두 D0에 묶여 있다.
  즉 **D0 없이는 D5를 닫을 수 없다** — D5는 코드가 아니라 판정 기준이 없어서 열려 있었다.
- **D5-c 잔여는 I5에 묶여 있다.** 본문이 밝힌 소유권 중복은 여전히 유효하며, 상대편
  I5-M5는 S0(코덱)만 끝나고 **S1(DataSystem 이중화)~S4(Editor)가 남았다**. 특히 S1이
  `DataSystem` 재질 경로를 건드리므로 지금 D5-c를 밀면 정면 충돌한다. **D5-c/D5-d는
  I5-M5 S1~S4 이후.**

**08-30: D0 완료로 그 블로커는 해제됐다.** 기준선 표와 목표치가 이 문서에 있고
(D0 절·§5 완료 기준 2), 게이트가 회귀 세트에 들어갔다.

권고 착수 순서: ~~D0 → D1(pak 필터 잔여) →~~ **D3-a → D3-b → D4**, D5-c/D5-d는 I5-M5
완료 뒤 합류, D6은 그 다음(§1.7 ⑤의 experiment 코덱 위치 결정 포함).

**08-30 진행 상황: D0·D1 완료.** 남은 것은 D3-a · D3-b · D4 · D5-c 잔여 · D5-d · D6 ·
D7(선택)이다. 다음은 **D3-a**이고, 이 슬라이스가 전체의 이음새다(§1.7 표가 보여주듯
저작 표면이 계속 자라므로 지연 비용이 단조 증가한다).

---

## 5. 완료 기준

1. **Player/Server 실행 중 text parser 호출 0** — ryml은 Editor authoring의 것.
2. **씬 전환 시간 D0 기준선 대비 수치 개선** — ~~목표치는 D0 후 확정.~~
   **08-30 D0 결과로 확정한 목표치**(모두 Release·같은 코퍼스·
   `verify-serialization-baseline.ps1` 판정):
   - Player 씬 전환에서 `SceneParse` **호출 0**(쿠킹 경로는 텍스트를 안 읽는다).
   - `SceneLoadTotal` **≥35% 감소**(Test1 평균 32.239 ms → 20.9 ms 이하,
     FT_Primitives 평균 12.279 ms → 8.0 ms 이하). 파싱 몫(60.0%/46.0%)을 전부 회수해도
     미귀속 19~20%와 ComponentLoad는 남으므로 100% 회수를 목표로 걸지 않는다.
     35%라는 문턱은 실행 간 변동폭(±7%)의 다섯 배라 노이즈와 구분된다.
   - 부팅 catalog **≥80% 감소**(평균 53.560 ms → 10.7 ms 이하). manifest 1건 읽기가
     `.meta` 240개 파싱을 대체하는 것이 D5-c의 정의이므로 이 항목만 감축 폭이 크다.
     단 이 항목의 변동폭이 ±18%로 가장 크므로 **3회 이상 재고 범위를 함께 적어야** 한다.
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
- **쿠킹 왕복 검사의 사각** — 리플렉션 밖 수기 파싱(~~ComponentFactory 27곳~~)이
  D3에서 완전히 수렴하지 않으면 왕복 비교가 그 필드를 못 본다. D3 판정에
  "수기 접근 잔존 목록 0 또는 명시 예외 표"를 포함한다.
  **08-30 정정(§1.7 ④): 실측 잔존은 1곳(`ComponentFactory.cpp:111` `ModuleBehavior`)뿐이라
  이 리스크는 사실상 해소됐다.** 다만 판정 조항은 유지한다 — 잔존 수가 적다는 것과
  왕복 검사가 그 1곳을 본다는 것은 다른 명제다.
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
