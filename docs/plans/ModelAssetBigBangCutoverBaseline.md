# 모델 자산 빅뱅 전환 — MBC0 기준선 (PHASE 3.75)

**작성 2026-09-02 · HEAD `1474c3c6` · 장비 `LANCE`(개발기) · Release exe 2026-09-02 12:58:59**

[`ModelAssetBigBangCutoverPlan.md`](ModelAssetBigBangCutoverPlan.md) §7 `MBC0`의 산출물이다.
cutover 전 상태를 **다시 뜨지 않는 archive**로 굳힌다. §8.4가 말하는 "cutover 전에 고정한
archived baseline"이 이 문서와 아래 두 기계 파일이다. MBC11은 새 단일 경로를 **별도 실행**으로
재고 이 표와 대조한다 — live legacy A/B 스위치는 두지 않는다.

| 기계 파일 | 내용 | 생성 |
|---|---|---|
| `Tools/regression/mbc0_corpus_baseline.json` | 모델 14·sidecar closure·저장 참조 28 파일의 GUID 분류 | `Export-MbcCorpusBaseline.ps1` (한 번만) |
| `Tools/regression/mbc_cutover_freeze.baseline.tsv` | 동결 표면 15종의 코드 접촉 수 | `verify-mbc-cutover-freeze.ps1 -Baseline` |

---

## 1. 변경 동결

### 1.1 동결 표면 접촉 수 (주석 제거 뒤, 제품 범위 = Engine·Editor·Tools·Player, RenderTests 제외)

| 표면 (§5.2 / §8.2) | 접촉 | 어디 |
|---|---:|---|
| `BuildLegacyModelFromExperiment` (역브리지) | 3 | DataSystem.h, ExperimentModelMigration.cpp |
| `CREATOR_EXPERIMENT_VERTEX` (A/B 스위치) | 1 | Mesh.h |
| `m_experimentMeshBindings` (병행 상태) | 6 | DataSystem.h, ExperimentModelMigration.cpp |
| `m_hashingMesh` (legacy binding 신원) | 14 | 12 파일 |
| `#include <assimp/…>` | 10 | 6 파일 |
| `DeterministicSubAssetId` (pseudo v5-as-v4) | 2 | ModelIdentityRefresher.cpp |
| `Uuid::FromName(` 호출 (SHA-1 v5 발급) | 1 | ModelIdentityRefresher.cpp |
| `ModelSceneBridge` | 0 | (§5.2가 이름 붙인 표면 — 현 코드에 심볼 없음) |
| `LoadModelViaExperiment` (legacy 폴백 창구) | 5 | 5 파일 |
| `[material.finalize]` 무조건 출력 | 0 | (미커밋 hack을 stash로 걷어낸 뒤 값) |
| `[mesh.resolve]` | 1 | |
| `[model.instantiate]` | 2 | |
| `[anim.tick]` | 1 | |
| `experiment.animlive` (상태 변경 진단 명령) | 5 | ConsoleCommandSystem.cpp 등 |
| vcpkg `assimp` 포트 | 1 | vcpkg.json |

래칫 규칙: **증가만 실패**. 접촉을 내린 슬라이스가 자기 커밋에서 `-Baseline`으로 내려 고정한다.
MBC9·MBC10 완료 조건은 이 표 전부 0이다.

### 1.2 하드 계약 (래칫 아님)

- model sidecar writer는 허용목록 둘뿐: `EditorAssetDatabase.cpp`(`CreateMetaLocked`),
  `ModelIdentityRefresher.cpp`. 셋째가 생기면 즉시 실패. MBC3가 둘을
  `ModelAssetAuthoringTransaction` 하나로 합치면 허용목록을 그것 하나로 좁힌다.
- 검사 전용 seam(`DeriveIdentityWithProfile`·`InsertUncheckedForTest`)은 `Engine/RenderEngine/Assets/`
  정의 밖 0건.
- `Assets/` 계층 안에 legacy 신원 API(`Uuid::FromName`·`IsAssetIdV4`·`CreateRandomV4`) 0건 —
  §0.1-1 "기존 GUID를 namespace/key로 쓰지 않는다"를 코드 위치로 강제한다.

### 1.3 동결 시점에 걷어낸 미커밋 변경 (stash `MBC0 freeze …`, 2026-09-02)

되돌린 것이지 잃은 것이 아니다 — `git stash list`의 `stash@{0}`에 그대로 있다.

| 파일 | 무엇이었나 | 왜 걷었나 |
|---|---|---|
| `Engine/SceneRuntime/MeshRenderer.cpp` | `OnDeserialized`가 재질 노드에서 model GUID를 미리 읽어 모델을 먼저 로드(임베디드 등록부 예열) | §6.2 "순서 해킹을 제거한다" — 폴백 위의 폴백. MBC7이 transaction으로 대체 |
| `Engine/RenderEngine/DataSystem.cpp` | `[material.finalize] <재질> slot=source…` 무조건 stdout | §5.2 "정상 제품 경로의 무조건 출력" 제거 대상. MBC10이 read-only 진단으로 대체 |
| `Dynamic_CPP/Assets/Models/Prim_*.glb.meta` ×8 | 최상위 GUID가 새 랜덤 v4로 바뀌고 `subAssets`가 통째로 사라진 sidecar | 아래 §3.1 — 씬은 HEAD GUID를 참조하므로 작업 트리 판은 고아 상태였다 |

---

## 2. corpus·저장 참조

### 2.1 모델 corpus (14, tracked 11)

| source | tracked | sidecar guid (legacy v4) | subAssets schema | 재질 | 임베디드 텍스처 |
|---|:-:|---|:-:|--:|--:|
| Animation/Ani_Mon_3_die.fbx | ✓ | 7238a117-… | 1 | 6 | 0 |
| Animation/Cha_Mon_5.fbx | ✓ | 2a91b60b-… | 1 | 0 | 0 |
| Models/Gunner_F_Mythic.glb | · | 935b883a-… | 1 | 2 | 6 |
| Models/Prim_Cone.glb | ✓ | 1cc56957-… | 1 | 1 | 1 |
| Models/Prim_Cube.glb | ✓ | 68b21a01-… | 1 | 1 | 1 |
| Models/Prim_Cylinder.glb | ✓ | 6723b433-… | 1 | 1 | 1 |
| Models/Prim_IcoSphere.glb | ✓ | a177fa64-… | 1 | 1 | 1 |
| Models/Prim_MatGrid.glb | ✓ | 0ecb57e2-… | 1 | 10 | 10 |
| Models/Prim_Plane.glb | ✓ | eef1dfaa-… | 1 | 1 | 1 |
| Models/Prim_Sphere.glb | ✓ | 79d6d261-… | 1 | 1 | 1 |
| Models/Prim_Suzanne.glb | ✓ | dfd9b94f-… | 1 | 1 | 1 |
| Models/Prim_Torus.glb | ✓ | 61079d31-… | 1 | 1 | 1 |
| Models/scene.glb | · | 89f6382b-… | **없음** | — | — |
| Models/SU_Mythic.glb | · | 8998b0a9-… | 1 | 1 | 3 |
| **합계** | | | 13/14 | **27** | **27** |

subasset key는 전부 `gltf/material/<ordinal>`·`gltf/image/<ordinal>`(FBX는 `fbx/…`) — §2.3이
**금지**한 배열 ordinal 키다. MBC2의 stable key 설계는 이 27+27건 전부를 다른 근거(exporter
persistent ID → semantic key → 새 authoring key)로 다시 세워야 한다. 전체 GUID·SHA-256은
JSON에 있다.

### 2.2 저장 참조 (.creator 14 · .prefab 9 · .asset 5)

GUID를 나르는 YAML 키 10종 중 `m_typeUUID`·`m_FileID`는 타입/인스턴스 식별자라 참조가 아니다.
나머지를 분류한 결과:

| 분류 | 건수 | 비고 |
|---|--:|---|
| model sidecar GUID 참조 | **8** | 전부 `FT_Primitives.creator`(tracked) — `m_fileGuid` 8건(Prim_Cube·Cone·Cylinder·IcoSphere·Plane·Sphere·Suzanne·Torus) |
| model **subasset** GUID 참조 | **0** | 재질·임베디드 텍스처 GUID를 참조하는 저장분이 corpus에 없다 |
| 다른 .meta GUID(외부 PNG·shadermeta·prefab 등) | 다수 | FT_Primitives는 외부 `.png.meta` 텍스처 8건 |
| **미해석**(어느 .meta도 없음) | **11** | 아래 |

미해석 11건 — 전부 gitignore 씬이고, 이미 고아다:

- `Test1.creator`: `m_textureGuid` 6건(190a70bb·452fe2ce·759f9743·b8accc52·e33297fe·f0c8eade),
  `m_fileGuid`/`m_Motion` = `d3a16695-…`(존재하지 않는 모델)
- `Test2.creator`: `m_fileGuid`/`m_Motion` = `5aab5ff1-…`(존재하지 않는 모델)

**MBC4에 주는 뜻:** 일회성 참조 rewrite의 실제 대상은 tracked 기준 **8건**(FT_Primitives)이다.
subasset 참조 0건이라 "재질/텍스처 GUID rewrite"는 corpus에 표적이 없다 — 합성 fixture로만
검증할 수 있다([[plan-target-may-be-already-dead]] 규칙: 대상 0건이면 실자산 게이트는 필터
유무와 무관하게 초록). Test1/Test2의 고아 11건은 rewrite가 **해석 실패로 거부**해야 하는
음성 fixture다.

---

## 3. 결함 증거 (계획서 §1이 인용한 것의 실측)

### 3.1 Prim sidecar 8개 — "신원 발급과 subasset refresh가 다른 writer"의 실제 형태

작업 트리에 있던 8개 sidecar는 최상위 GUID가 전부 새 랜덤 v4였고 `subAssets` 블록이
사라졐으며 `importSettings.timestamp`만 갱신돼 있었다. 씬 `FT_Primitives.creator`는 HEAD의
GUID(1cc56957… 등 7건 일치)를 참조한다 → 작업 트리 판은 씬 참조를 전부 고아로 만든 상태였다.
`Prim_Cube.glb.meta`만 살아남았다.

원인 경로(코드 대조): `ResolveOrCreateGuid`는 sidecar에 `guid:`가 있으면 그것을 지키므로,
새 GUID가 나왔다는 것은 **sidecar가 없는 상태에서 `CreateMetaLocked`가 불렸다**는 뜻이다.
그 상태를 만드는 알려진 경로가 하나 있다 — efsw 워처가 원자 게시(`.tmp` → replace)를 목적지
경로의 Delete로 오독해 sidecar를 떨어뜨리는 `HandleDeleted`
(`verify-prefab-identity-injection`이 같은 결함을 프리팹에서 재현·고정했다). 2026-09-02
`AssetCooker --refresh-model-identities`가 에디터가 켜진 채 8개 sidecar를 원자 게시했다.
이것이 §3.2-8 "파일 watcher에는 최종 generation 하나만 보인다"와 §3.3 writer 단일화의 근거다.

### 3.2 subasset key가 ordinal이다

§2.1 표대로 27+27건 전부 `gltf/material/N`·`gltf/image/N`. 재익스포트로 순서가 바뀌면 같은
key가 다른 재질을 가리킨다. 결정적 유도(pseudo v5)는 재임포트 **멱등성**만 줬고 **안정성**은
주지 못했다 — 두 성질은 다르다.

---

## 4. 성능 기준선 (Release, 같은 장비·같은 corpus)

측정 도구는 기존 것 둘이다: `verify-serialization-baseline.ps1 -Baseline`(D0 하네스),
`experiment.bench <모델> 5`(14 모델 한 프로세스). 원시 출력은 세션 scratchpad에 있고 요약만
여기 적는다.

### 4.1 부팅·씬·프리팹 (D0 하네스, iterations 5, warmup 1)

| 항목 | 값 |
|---|--:|
| 부팅 AssetCatalog (meta 226) | 43.25 ms (191 µs/meta) |
| Test1.creator SceneLoadTotal / iter | 31.54 ms (parse 18.01 · entity 1.70 · component 4.81 · 미귀속 7.0) |
| FT_Primitives.creator SceneLoadTotal / iter | 48.18 ms (parse 4.50 · entity 0.28 · **component 41.06** · 미귀속 2.3) |
| NestedProbeParent PrefabInstantiate / iter | 3.26 ms |

FT_Primitives의 component 41 ms가 모델 8개 로드다 — cutover가 바꾸는 값이 이것이다.

### 4.2 모델별 로드 (`experiment.bench`, 반복 5, ms)

★ 읽는 법. 실행 시점에 `.asset` 쿠킹 캐시가 없어 legacy 1회차는 Assimp 파싱이고 그 회차가
`.asset`을 써 두므로 **2~5회차는 쿠킹 읽기**다. 따라서 `legacy min` = 쿠킹 읽기, 진짜 Assimp
소스 파싱은 `5·avg − 4·min`으로 역산한 **추정치**다(bench가 "Assimp"라 적은 값은 min이라
소스가 아니다 — 하네스의 표기 결함, MBC11 전에 고칠 것). experiment는 소스 디코드 전체.

| 모델 | legacy `.asset` 읽기 (min) | legacy Assimp 소스 (추정) | experiment 소스 (min) | experiment cook 쓰기 (min / bytes) |
|---|--:|--:|--:|--:|
| Ani_Mon_3_die.fbx | 89.7 | ~190 | 44.0 | 1.6 / 695,912 |
| Cha_Mon_5.fbx | 20.7 | ~34 | 4.7 | 0.2 / 53,944 |
| Gunner_F_Mythic.glb | 9.5 | ~158 | 55.6 | 2.5 / 1,624,120 |
| Prim_Cone.glb | 1.2 | ~25 | 0.7 | 0.1 / 7,208 |
| Prim_Cube.glb | 1.1 | ~27 | 0.3 | 0.1 / 2,480 |
| Prim_Cylinder.glb | 1.2 | ~27 | 1.0 | 0.1 / 13,056 |
| Prim_IcoSphere.glb | 1.0 | ~19 | 0.5 | 0.1 / 16,376 |
| Prim_MatGrid.glb | 4.8 | ~39 | 16.1 | 1.4 / 856,928 |
| Prim_Plane.glb | 0.7 | ~21 | 0.2 | 0.1 / 1,408 |
| Prim_Sphere.glb | 1.2 | ~24 | 2.0 | 0.3 / 88,504 |
| Prim_Suzanne.glb | 0.9 | ~23 | 1.4 | 0.2 / 63,984 |
| Prim_Torus.glb | 0.9 | ~20 | 2.3 | 0.3 / 127,720 |
| scene.glb | 54.3 | ~859 | 388.0 | 28.6 / 12,411,560 |
| SU_Mythic.glb | 5.8 | ~76 | 36.8 | 2.6 / 1,789,752 |

| 프로세스 | 값 |
|---|--:|
| bench 14 모델 전체 peak working set | 1,351 MB |
| bench 전체 경과 | 19 s |

bench가 만든 `.asset` 14개는 측정 뒤 지웠다(첫 export 뒤 나타난 파생물 — 기준선 상태 복원).

### 4.3 이번에 재지 못한 것 (MBC11 전에 닫아야 하는 계측 공백)

- **새 경로의 cooked(warm) 읽기 시간.** bench는 experiment cook **쓰기**만 잰다. 새
  `ModelAssetGeneration` cold cooked load는 MBC5가 계측 지점을 만들어야 한다.
- **frame CPU record·GPU pass timing.** 씬을 올린 뒤 프레임 시간을 stdout으로 내는 CLI가 없다
  (`dx12.scene`은 커버리지, `profile.stats`는 프로파일러 자기 비용, `gpu.census`는 VRAM 계수).
  PHASE 4 `BASE-0` 하네스가 같은 축을 만들기로 돼 있다 — MBC11이 그것을 쓰거나 최소 CLI를
  먼저 세운다. **비회귀 판정은 이 축이 생기기 전에는 할 수 없다**고 적어 둔다.
- **peak VRAM.** `gpu.census`는 실행 중 VRAM만 남기며 타입별 집계는 종료 리포트다. 위와 같이
  MBC11 계측 항목.

### 4.4 예산 (§8.4 — 여기서 고정)

| 예산 | 기준 | 판정 |
|---|---|---|
| B1 cold source import (모델별) | §4.2 `legacy Assimp 소스(추정)` | 새 경로 ≤ 기준 (experiment는 이미 14/14 우세) |
| B2 cooked load (모델별) | §4.2 `legacy .asset 읽기(min)` | 새 cooked generation load ≤ 기준 × 1.25 |
| B3 씬 로드 | FT_Primitives 48.18 ms · Test1 31.54 ms | ≤ 기준 × 1.10 (D0 실측 흔들림 ±7%) |
| B4 부팅 catalog | 43.25 ms / 226 meta | ≤ 기준 × 1.10 (±18% 흔들림 — 참고 판정) |
| B5 peak working set(bench 시나리오) | 1,351 MB | ≤ 기준 (경로가 하나로 줄면 내려가야 정상) |
| B6 frame CPU / GPU pass / peak VRAM | **미측정** | MBC11이 계측 축을 세운 뒤 cutover 직전 archive 대비 비회귀 |

기준을 못 맞추면 cutover를 연기한다. legacy fallback을 제품에 되살리는 것으로 맞추지 않는다.

---

## 5. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-02 | 신설(MBC0). 동결 표면 15종·하드 계약 3종·stash 처리 3종, corpus 14/참조 28 분류(모델 참조 8·subasset 참조 0·고아 11), Prim sidecar 손상 원인 경로, Release 성능 기준선과 예산 B1~B6 |
