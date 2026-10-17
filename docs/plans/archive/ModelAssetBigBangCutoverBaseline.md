# 모델 자산 빅뱅 전환 — MBC0 기준선 (PHASE 3.75)

> **보관 · 완료 계획의 기준선 (정리: 2026-09-06).** PHASE 3.75 MBC0/MBC11의 동결·재유도 근거. 기존 예산·회귀 스크립트의 참조 자료로 계속 보존한다.
> [보관 색인](README.md) · [활성 계획과 대시보드](../../RefactoringPlanDashboard.html#doc-index)

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

**2026-09-03 MBC9 재기준**: 제거 표면(역브리지·A/B 스위치·병행 바인딩·Assimp include·
`ModelSceneBridge`·`LoadModelViaExperiment`·vcpkg port)은 전부 0이다. 남은 비영 항목은
`m_hashingMesh` 10(모델이 아닌 절차 지오메트리 — 스프라이트 쿼드·지형·기즈모·격리 하네스의
legacy `Mesh` 캐시 키. 모델 지오메트리는 `{ModelId,MeshId,generation}`만 탄다)과 진단 출력
4건(`[mesh.resolve]`·`[model.instantiate]`·`[anim.tick]` 각 1, `experiment.animlive` 5 —
MBC10의 read-only화 대상)이다.

**2026-09-03 MBC10 재기준**: 진단 출력 표면 4종은 제품 경로에서 0이 됐고(계수 →
`assets.modeldiag` 스냅샷), `experiment.animlive` 표면은 래칫에서 빠지고 "publish를 부르지
않는다"는 하드 계약으로 바뀌었다. 제거 표면 12종은 하드 0이다. 남은 비영 항목은
`m_hashingMesh` 10(허용목록 10파일 — 밖은 하드 실패)뿐이다.

### 1.2 하드 계약 (래칫 아님)

- model sidecar writer 허용목록: legacy 둘(`EditorAssetDatabase.cpp`의 `CreateMetaLocked`,
  `ModelIdentityRefresher.cpp`)과 MBC2가 더한 v2 코덱(`Assets/ModelSidecarV2.cpp`). 넷째가
  생기면 즉시 실패. MBC3가 legacy 둘을 제거하고 `ModelAssetAuthoringTransaction`이 v2 코덱을
  통해서만 쓰게 되면 허용목록은 그 하나로 좁힌다.
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

### 4.4a B1·B2 축 재유도 (2026-09-04 — §4.4의 개정, 사용자 결정)

§4.5의 실측이 B1·B2를 붉게 냈고, 단계 분해가 그 붉음의 원인 대부분이 **성능 회귀가
아니라 축 불일치**임을 보였다. 기준값이 잰 일과 새 경로가 재는 일이 애초에 다르다.

| 기준값 | 그것이 실제로 잰 일 | 새 경로가 추가로 하는 일 |
|---|---|---|
| B1 `legacy Assimp 소스(추정)` | 프로토타입 `experiment.bench` min 역산 = **디코드** | 신원 유도 · corpus 충돌 검사 · staging 쓰기 · staging 재판독 검증 · 원자 게시 |
| B2 `legacy .asset 읽기(min)` | 단일 이진 파일 읽기 + 디코드. **텍스처를 읽지 않았다**(재질 바인딩 때 텍스처 로더가 따로 읽었다) | 임베디드 텍스처 디코드 · 신원/epoch 검증 · artifact SHA-256 검증 |

총합끼리 비교하면 이 셋이 한 숫자에 뭉쳐 "새 경로가 10배 느리다"로 읽힌다. 그래서
**같은 일끼리만 비교 예산으로 판정하고, legacy에 대응물이 없는 칸은 비회귀로만 본다.**

| 칸 | 재는 단계 | 판정 |
|---|---|---|
| **B1a** 비교 | author `source-read+decode` | ≤ §4.2 legacy Assimp 소스 추정 |
| **B1b** 비회귀 | author 총합 − 디코드 (신원·staging·검증·원자 게시) | archive × 1.25 (또는 +2 ms) 안 |
| **B2a** 비교 | cooked `cemc-read` + `cemc-decode+validate` + `materials+meshes+skeleton` + `assemble` | ≤ §4.2 legacy `.asset` 읽기 min × 1.25 |
| **B2b** 비회귀 | cooked `identity+sidecar` + `cemc-sha` | archive × 1.25 (또는 +2 ms) 안 |
| **B2c** 이관 | cooked `textures-read+sha+decode` | **PHASE 12 T1a/T2**가 판정(§8 "런타임 텍스처 로드 < 1 ms/장"). 여기서는 기록·비회귀만 |

★ **이 개정이 지키는 선.** ① 예산을 "텍스처 제외"로 슬며시 넓히는 것이 아니라,
어느 칸이 어느 판정을 받는지 게이트 코드와 이 표에 함께 못박는다 — 이관한 칸도
사라지지 않고 archive에 모델별 값으로 남아 회귀하면 붉어진다. ② 비회귀 archive는
"지금 값"을 정답으로 굳히는 것이라 **첫 기록은 옳다는 증거가 아니다**(B6와 같은
규약). 그래서 archive를 뜨기 전에 실제 낭비를 먼저 걷었다 — 저작마다 도는 corpus
재스캔과 게시 직전의 source 전량 재해시(§4.6). ③ 붉은 실행은 archive로 뜨지 않는다.

★ **`cemc-read`와 `cemc-sha`를 코드에서 갈랐다.** 한 칸에 묶여 있으면 "읽기는
비교 대상이고 해시는 아니다"를 주장만 할 수 있고 보일 수 없다. 갈라 보니 scene은
12.4 MB CEMC 읽기가 3 ms대, SHA-256이 65 ms대다 — 이 저장소의 SHA-256은 순수 스칼라
구현(`Utility_Framework/Sha256.h`, 약 180 MB/s)이다.

★ **가속은 이 장비에서 답이 아니다.** 후보 ④(CEMC SHA 청크화·가속)를 재기 전에
CPU를 확인했다: **Xeon W-2223(Cascade Lake-W)에는 SHA-NI가 없다.** Intel은 Goldmont와
Ice Lake 이후에만 넣었고 Skylake-SP 계열 워크스테이션 파트는 빠져 있다. 즉 SHA-NI
경로를 넣어도 이 기준선 장비에서는 스칼라로 떨어지고, CNG(BCrypt)로 바꿔도 같다.
스칼라 튜닝의 상한은 1.5~2배라 B2a를 가르지 못한다 — **축을 가르는 것이 유일하게
정직한 해법이었다.** 다른 장비에서 재는 사람이 이 판정을 다시 하지 않도록 적어 둔다.

---

### 4.5 MBC11 실측 (Release, 2026-09-03, `verify-model-cutover-budget`, 반복 5)

★ 측정 방법. legacy 런타임이 제품에 없으므로 §4.4의 기준값을 게이트 상수로 박고 새 경로만
잰다. B1/B2는 `Dynamic_CPP`의 Models·Animation·Shaders·Materials·ProjectSetting을 임시
프로젝트 사본으로 복사한 뒤 `assets.modelbench <copy>/Assets/Models 5 author`가 in-process
`assets::AuthorModelAsset`을 5회 돌리고(B1 = min), 방금 게시한 generation을
`assets::LoadModelAssetGeneration`으로 5회 읽는다(B2 = min). 작업 트리 Library는 건드리지
않는다. 단계 분해는 두 결과의 `phases`(MBC11이 닫은 §4.3 계측 공백)다. B3/B4는
`verify-serialization-baseline`, B5는 저작 프로세스 peak working set, B6는 `dx12.scene`.
corpus 재임포트(2026-09-03 17:40, CEMC v4→v5, 14/14 성공, 신원 324개 전부 보존, generation만 상승)
뒤의 값이다.

| 예산 | 실측 | 판정 |
|---|--:|---|
| B3 Test1 SceneLoadTotal / iter | 15.16 ms (기준 31.54 → ≤ 34.69) | 통과 |
| B3 FT_Primitives SceneLoadTotal / iter | 11.76 ms (기준 48.18 → ≤ 53.00) | 통과 — component 41 ms가 모델 8개 typed 로드로 내려갔다 |
| B4 부팅 AssetCatalog (meta 226) | 31.08 ms (≤ 47.58; 실행마다 29~52 ms로 흔들림) | 통과 |
| B5 저작+로드 프로세스 peak working set | 832.8 MB (≤ 1,351) | 통과 |
| B6 frame/GPU/VRAM (archive 기준선) | 메시 업로드 10개 / 1,075 KB · 커버리지 49,684 · VRAM 1,145 MB | 기록 — `mbc11_perf_archive.json` |
| B2w 작업 트리 Library 재로드(참고) | 14/14 로드, 실패 0 | 통과 |
| B1 cold source import | 아래 표 — **10/14 초과** | **미충족** |
| B2 cooked generation load | 아래 표 — **12/14 초과** (텍스처 디코드 제외 시 4/14) | **미충족** |

| 모델 | B1 저작 min | 기준 | 판정 | 그중 소스 디코드 | B2 cooked min | 기준(×1.25) | 판정 | 그중 텍스처 디코드 | 텍스처 제외 |
|---|--:|--:|---|--:|--:|--:|---|--:|--:|
| Ani_Mon_3_die | 71.6 | 190 | ok | 33.1 | 5.2 | 112.12 | ok | 0.0 | 5.2 ok |
| Cha_Mon_5 | 33.8 | 34 | ok | 5.4 | 0.8 | 25.88 | ok | 0.0 | 0.8 ok |
| Gunner_F_Mythic | 150.9 | 158 | ok | 51.9 | 54.1 | 11.88 | **초과** | 42.5 | 11.7 ok |
| Prim_Cone | 25.9 | 25 | **초과** | 1.0 | 13.4 | 1.50 | **초과** | 12.8 | 0.7 ok |
| Prim_Cube | 26.0 | 27 | ok | 0.7 | 13.2 | 1.38 | **초과** | 12.5 | 0.6 ok |
| Prim_Cylinder | 27.0 | 27 | **초과** | 1.1 | 13.7 | 1.50 | **초과** | 12.9 | 0.8 ok |
| Prim_IcoSphere | 26.6 | 19 | **초과** | 1.0 | 13.3 | 1.25 | **초과** | 12.5 | 0.8 ok |
| Prim_MatGrid | 71.8 | 39 | **초과** | 19.1 | 7.7 | 6.00 | **초과** | 1.2 | 6.5 **초과** |
| Prim_Plane | 27.2 | 21 | **초과** | 0.6 | 13.5 | 0.88 | **초과** | 12.7 | 0.7 ok |
| Prim_Sphere | 32.2 | 24 | **초과** | 2.3 | 13.9 | 1.50 | **초과** | 12.5 | 1.4 ok |
| Prim_Suzanne | 28.7 | 23 | **초과** | 2.3 | 13.8 | 1.12 | **초과** | 12.7 | 1.1 ok |
| Prim_Torus | 31.6 | 20 | **초과** | 3.4 | 14.8 | 1.12 | **초과** | 13.2 | 1.6 **초과** |
| SU_Mythic | 142.5 | 76 | **초과** | 50.9 | 45.4 | 7.25 | **초과** | 32.4 | 12.9 **초과** |
| scene | 1,378.5 | 859 | **초과** | 461.8 | 765.4 | 67.88 | **초과** | 665.3 | 100.0 **초과** |

**B2 초과의 실체.** cooked 로드의 `textures-read+sha+decode`가 embedded texture 1장(512² PNG)당
≈12.8 ms, Gunner 6장 42.5 ms, scene 69장(22.5 MB) 663 ms로 로드의 93~98%다(WIC 디코드 +
RGBA8 변환, ≈49 ns/픽셀). legacy `.asset` 읽기 기준값은 텍스처를 **포함하지 않았다**(legacy는
재질 바인딩 때 텍스처 로더가 따로 읽었다) — 기준이 재는 축과 새 경로가 재는 축이 다르다. 텍스처를
빼면 10/14가 예산 안이고, 남는 4개는 CEMC 읽기+SHA-256(scene 12.4 MB 70 ms,
SU 1.8 MB 9.6 ms)과 Gunner·Torus·MatGrid의 소수점 초과다.

**B1 초과의 실체.** 소스 디코드(`source-read+decode`)는 14/14 기준 안이다(Gunner 54.9 vs 158,
scene 445 vs 859). 초과분은 저작 트랜잭션이 더한 고정·비례 비용이다 — Prim 한 개 기준
`corpus-collision-scan` ≈ 6.6 ms(사본 345파일 재귀 순회·sidecar 14개 재파싱, 저작마다),
`stage-write` ≈ 7.5 ms(파일 4~5개 생성), `stage-verify` ≈ 5.2 ms(staging 재판독·SHA·CEMC 재디코드),
`publish` ≈ 2.5 ms(rename 2회 + **source 전체 재읽기·재해시**). scene(35 MB 소스)은 stable key
127 ms, sidecar/draft 121 ms, stage-write 163 ms, stage-verify 222 ms, publish 197 ms가 디코드
445 ms 위에 얹힌다.

| 단계 | Prim_Cone | Gunner_F_Mythic | scene |
|---|--:|--:|--:|
| 저작 `lock+header+sidecar` | 0.8 | 1.1 | 3.4 |
| 저작 `source-read+decode` | 1.0 | 51.9 | 461.8 |
| 저작 `stable-keys+identity` | 0.3 | 12.7 | 132.1 |
| 저작 `corpus-collision-scan` | 6.7 | 7.3 | 4.3 |
| 저작 `sidecar+draft` | 0.7 | 8.6 | 118.6 |
| 저작 `cook+textures+record` | 1.8 | 10.6 | 66.2 |
| 저작 `stage-write` | 6.3 | 17.9 | 157.3 |
| 저작 `stage-verify` | 5.3 | 20.3 | 216.9 |
| 저작 `publish` | 2.9 | 18.8 | 201.2 |
| cooked `identity+sidecar` | 0.50 | 0.91 | 5.53 |
| cooked `cemc-read+sha` | 0.10 | 8.62 | 68.93 |
| cooked `cemc-decode+validate` | 0.02 | 1.57 | 20.25 |
| cooked `materials+meshes+skeleton` | 0.01 | 0.30 | 3.48 |
| cooked `textures-read+sha+decode` | 12.79 | 42.47 | 665.34 |
| cooked `assemble` | 0.01 | 0.01 | 0.02 |

**corpus 재임포트(2026-09-03).** 같은 날 다른 세션이 glTF UV 보존을 위해 CEMC `kFormatVersion`을
4→5로 올려 작업 트리 Library 14개가 "포맷 버전 4 != 5 — 재임포트 필요"로 거부되고 있었다(B6·B2w
측정 불가, corpus 게이트 3종 RED). `AssetCooker --author-model-asset`로 14/14를 재임포트했다 —
**assetId 324개(모델 14·subasset 310)가 전부 동일**하고 generation만 올랐다(Prim 1→2, Gunner 15→16,
Cha_Mon_5 3→4). 저장 씬/프리팹 참조는 모델 assetId를 쓰므로 rewrite가 필요 없었다. 재임포트 뒤
`verify-model-asset-generation`(corpus 14/14 loaded, mesh 130·material 52·texture 96·skeleton 4·
animation 28·descriptor 356), `verify-experiment-cooked-catalog`, `verify-experiment-asset-cooker`가
초록으로 돌아왔다. 이전 generation 디렉터리는 남아 있다(Library는 gitignore).

**B6 기준선의 뜻.** archive는 "지금 값"을 정답으로 굳히는 것이라 비회귀 판정에만 쓴다 — 첫 기록이므로
이 값 자체가 옳다는 증거는 아니다. FT_Primitives + Gunner 씬에서 메시 업로드 10개·1,075 KB가 전량
generation 경로였고(`dx12.scene` 통과), 이후 실행은 ×1.10 안이어야 한다. 게이트는 B6가 붉은 실행을
archive로 뜨지 않는다(0 값 기준선 방지).

**cutover 판정.** §4.4 규칙("기준을 못 맞추면 cutover를 연기한다. legacy fallback을 되살리지
않는다")에 따라 MBC11의 cutover는 보류다. 예산을 맞출 후보(계획 결정은 별도): ① cooked 텍스처를
디코드 완료 형식(RGBA8/BC + mip)으로 게시해 로드가 memcpy가 되게 한다(B2 12/14의 원인),
② corpus collision registry를 프로젝트 단위로 캐시해 저작마다 재스캔하지 않는다(B1 고정 6.6 ms),
③ publish 단계의 source 재해시를 크기+mtime 지문으로 바꾸고 stage-verify의 CEMC 재디코드를 SHA만
남긴다(B1 비례 비용), ④ CEMC 읽기의 SHA-256을 mmap/청크 해시로(scene 70 ms). ①~③이 닫히면
B1/B2를 다시 잰다.

★ **이 판정은 §4.4a·§4.6이 승계했다(2026-09-04).** ②와 ③은 여기서 했고, ①은 **PHASE 12
T1a로 이관**했다(그 artifact 포맷의 소유자라, 여기서 만들면 T1a가 다시 정의할 포맷이 하나
더 생긴다), ④는 **기각**했다(이 장비에 SHA-NI가 없다 — §4.4a). ③의 후반부 "stage-verify의
CEMC 재디코드를 SHA만 남긴다"는 하지 않았다 — 그것은 낭비가 아니라 "게시할 artifact가
실제로 읽히는가"를 재는 내구성 검증이고, 축을 가른 뒤에는 예산을 막지 않는다.

### 4.6 MBC11 재측정 — 축 재유도 뒤 (Release, 2026-09-04, 반복 5)

§4.4a 규칙으로 판정한다. **판정 전에 3.75가 소유한 낭비 둘을 먼저 걷었다** — archive는
"지금 값"을 정답으로 굳히므로 낭비를 굳혀서는 안 된다.

1. **corpus collision 검사의 sidecar 파싱 캐시.** 저작마다 asset root를 재순회하며 schema v2
   sidecar 14개를 다시 파싱하고 있었다. 순회는 남기고(새로 생긴 sidecar를 봐야 충돌을 잡는다)
   파싱만 `(크기, mtime)` 지문으로 캐시했다. 폐포 재검증은 캐시된 문서에도 매번 돈다 —
   `ValidateModelSidecarV2Closure`가 epoch header를 인자로 받으므로 캐시가 그 판정을 대신하면
   header가 바뀐 뒤에도 옛 판정을 재사용한다.
   **효과: `corpus-collision-scan` 평균 6.93 → 3.92 ms(14모델, min 4.18 → 1.88, max 8.73 → 6.06).**
   ★ **착수 가정이 절반 틀렸다.** "6.6 ms는 파싱이 지배한다"고 보고 캐시를 넣었는데 절감이
   3 ms뿐이다 — **남은 4 ms는 파싱이 아니라 순회다.** asset root 전체(사본은 Materials 25 MB
   포함 345파일)를 `recursive_directory_iterator`로 도는 비용이고, 모델 sidecar는 어디에나
   있을 수 있으므로 순회 범위를 좁힐 근거가 없다. 순회 결과를 캐시하면 **새로 생긴 sidecar를
   놓쳐** 충돌 검사가 눈멀므로 하지 않았다. 축을 가른 뒤 이 4 ms는 어느 예산도 막지 않는다.
   ★ **못 잡는 것:** 캐시가 낡은 문서를 재사용하는 상황을 재는 게이트가 없다. 캐시는
   프로세스 수명이고 게이트들은 저작마다 AssetCooker를 새로 띄우므로(프로세스마다 빈 캐시)
   지문 검사를 통째로 지우는 변이도 초록일 것이다. 위험이 작다고 판단한 근거는 둘이다 —
   후보 자신의 sidecar는 스캔에서 제외되고(`entry.path() == candidateSidecarPath`), 다른
   모델의 신원은 generation이 올라도 **바뀌지 않는다**(stable key가 같으면 같은 UUIDv8).
   즉 낡은 항목이 잡아낼 것을 놓치려면 같은 프로세스 안에서 다른 모델의 **소스가 바뀌어
   신원이 달라지고**, 그 sidecar의 크기와 mtime이 **둘 다 그대로**여야 한다.
2. **게시 직전 source race 검사.** 소스 전체를 다시 읽어 SHA-256을 다시 계산했다(scene
   35 MB ≈ 200 ms). 검사의 뜻은 "transaction 도는 동안 소스가 바뀌었나"이고 신원 유도가
   아니므로(신원의 `sourceFingerprint`는 처음 읽은 바이트에서 이미 나왔다) `(크기, mtime)`
   대조를 앞에 두고 다르면 옛 방식대로 전량 재해시한다 — 내용은 그대로인데 touch만 된
   파일을 거짓 거부하지 않기 위해서다. **약해지는 지점:** 크기와 mtime을 둘 다 보존하며
   내용만 바꾼 쓰기는 통과한다(파일 시스템이 쓰기에 mtime을 올리므로 실수로 그렇게 되는
   경로는 없다). 이 칸에는 게이트가 없다 — 주입 지점이 rollback용이고 "게시 직전 소스
   바꾸기"는 주입 어휘에 없다. 못 잡는 것으로 적어 둔다.
   **효과: `publish` 단계가 14모델 합계 302.6 → 48.1 ms(평균 21.6 → 3.4). scene 212.9 → 2.8,
   Gunner 19.5 → 3.1, SU 18.7 → 4.3.** 저작 총합이 내려간 것의 본체가 이쪽이다.

| 저작 트랜잭션 비용(B1b) | 이전 | 이후 |
|---|--:|--:|
| scene | 1,001 ms | **770 ms** |
| SU_Mythic | 104 | **82** |
| Gunner_F_Mythic | 112 | **104** |
| Prim_MatGrid | 64 | **53** |
| Prim(작은 것) | 30~31 | **25~28** |

★ **이 축의 흔들림이 크다.** 같은 바이너리·같은 자산으로 20분 안에 두 번 잰 값이
Prim_Cone 27.4와 42.1 ms다(`stage-write` 10.0↔14.9, `stage-verify` 5.1↔8.4 — 작은 파일
4~5개를 만들고 다시 읽는 NTFS 비용이 지배하고 디스크 상태에 딸린다). 그래서 **archive는
관측된 상단 근처를 들고 있고 판별력이 낮다 — 큰 회귀만 잡는다.** archive 값을 목표로
읽지 말 것. 여유가 ×1.25(또는 +2 ms)인 이유도 이것이다.

**판정.**

| 예산 | 실측 | 판정 |
|---|--:|---|
| B1a source decode | 14/14 예산 안 | **통과** |
| B2a CEMC 읽기·디코드·조립 | 14/14 예산 안 | **통과** |
| B3 Test1 / FT_Primitives | 15.22 / 12.06 ms (≤ 34.69 / 53.00) | 통과 |
| B4 부팅 AssetCatalog | 30.48 ms (≤ 47.58; 실행마다 29~52) | 통과 |
| B5 peak working set | 833.3 MB (≤ 1,351) | 통과 |
| B6 frame/GPU/VRAM | 메시 업로드 10개 / 1,075 KB · 커버리지 49,684 · VRAM 1,144 MB | 비회귀 통과 |
| B1b 저작 트랜잭션 | 아래 표 | archive 기록 |
| B2b 신원·해시 검증 | 아래 표 | archive 기록 |
| B2c 임베디드 텍스처 디코드 | 아래 표 | **PHASE 12 이관** · archive 기록 |

| 모델 | B1a 디코드 | 기준 | B1b 트랜잭션 | 저작 총합 | B2a 로드 | 기준(×1.25) | B2b 검증 | B2c 텍스처 | 로드 총합 |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| Ani_Mon_3_die | 40.24 | 190 | 32.41 | 72.65 | 1.68 | 112.13 | 3.67 | 0.00 | 5.37 |
| Cha_Mon_5 | 6.19 | 34 | 28.05 | 34.24 | 0.16 | 25.88 | 0.59 | 0.00 | 0.77 |
| Gunner_F_Mythic | 67.14 | 158 | 99.76 | 166.89 | 5.92 | 11.88 | 17.69 | 46.21 | 70.15 |
| Prim_Cone | 1.86 | 25 | 42.14 | 44.00 | 0.17 | 1.50 | 0.76 | 3.63 | 4.59 |
| Prim_Cube | 1.15 | 27 | 45.17 | 46.31 | 0.14 | 1.38 | 0.83 | 3.59 | 4.58 |
| Prim_Cylinder | 2.07 | 27 | 45.31 | 47.38 | 0.17 | 1.50 | 0.74 | 3.38 | 4.32 |
| Prim_IcoSphere | 1.19 | 19 | 34.73 | 35.92 | 0.16 | 1.25 | 0.74 | 2.71 | 3.62 |
| Prim_MatGrid | 19.64 | 39 | 59.49 | 79.13 | 2.25 | 6.00 | 4.60 | 1.37 | 8.24 |
| Prim_Plane | 0.82 | 21 | 32.06 | 32.88 | 0.11 | 0.88 | 0.76 | 3.17 | 4.06 |
| Prim_Sphere | 2.64 | 24 | 36.97 | 39.61 | 0.27 | 1.50 | 0.92 | 2.73 | 3.93 |
| Prim_Suzanne | 3.01 | 23 | 31.69 | 34.70 | 0.27 | 1.13 | 0.93 | 2.84 | 4.05 |
| Prim_Torus | 3.11 | 20 | 30.96 | 34.07 | 0.38 | 1.13 | 1.10 | 2.59 | 4.09 |
| SU_Mythic | 62.39 | 76 | 97.38 | 159.77 | 5.25 | 7.25 | 13.07 | 32.32 | 51.28 |
| scene | 478.81 | 859 | 782.80 | 1,261.61 | 53.84 | 67.88 | 85.48 | 550.60 | 691.78 |

★ 이 표는 **archive를 뜬 실행**(2026-09-04 18:40)의 값이다 — `mbc11_perf_archive.json`과
같은 실행이라야 비회귀 기준선과 문서가 어긋나지 않는다. 같은 날 20분 앞선 실행은 더 빨랐다
(Prim_Cone 저작 총합 28.3 vs 44.0, scene B2a 38.4 vs 53.8) — 위의 흔들림 경고를 보라.

★ **B2c가 아직 로드의 66~80%다**(Prim 2.6~3.6 ms · Gunner 46 ms · scene 551 ms). §4.5
시점(장당 12.8 ms)보다 3.5~4배 빨라진 것은 같은 날 다른 트랙이 디코더 경계를 세운
결과이고(`TextureCodecBoundaryDesign` 축 A), 구조는 그대로다 — artifact가 PNG이고
로드가 매번 디코드한다. 이 칸의 완료선은 PHASE 12 §8 "런타임 텍스처 로드 < 1 ms/장"이다.

★ **B2b(신원·epoch·SHA 검증)가 B2a보다 크다** — scene 85 ms vs 54 ms. 그 85 ms의
대부분이 CEMC 12.4 MB의 SHA-256(`cemc-sha` 77.7 ms)이고 이 저장소의 SHA-256은
스칼라(≈ 160~180 MB/s)다 — 같은 파일의 순수 읽기는 `cemc-read` 21 ms다.
§4.4a에 적은 대로 이 장비(Xeon W-2223)에는 SHA-NI가 없어 가속으로는 못 내린다.
내리는 길은 둘 — 로드 시점 전량 해시를 빠른 체크섬으로 바꾸거나(generation record
schema 변경 → 재임포트), 원자 게시를 믿고 로드 검증을 줄이는 것이다. 어느 쪽도
지금 예산을 막지 않으므로 이 페이즈에서 하지 않고 수치만 남긴다.

---
## 5. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-04 | **§4.4a 축 재유도 · §4.5 정정 · §4.6 재측정.** B1·B2의 붉음이 성능 회귀가 아니라 축 불일치임을 단계 분해가 보였고(기준값은 디코드만·텍스처 미포함·해시 미포함), 사용자 결정으로 판정 축을 갈랐다 — 비교 예산 B1a·B2a(**각각 14/14 통과**), 비회귀 archive B1b·B2b·B6, PHASE 12 이관 B2c. 판정 전에 3.75가 소유한 낭비 둘(저작마다 도는 corpus 재스캔·게시 직전 source 전량 재해시)을 걷어 저작 트랜잭션 비용을 scene 1,001 → 770 ms로 내렸다. 후보 ④는 기각 근거를 함께 적었다 — **이 장비(Xeon W-2223 Cascade Lake-W)에 SHA-NI가 없다.** §4.5의 텍스처 디코드 수치(장당 12.8 ms)는 같은 날 디코더 경계 슬라이스로 2.5~3.2 ms가 됐다(구조는 그대로) |
| 2026-09-03 | MBC11 실측 §4.5 추가(corpus 재임포트 뒤 재측정) — B3(14.6/20.0 ms)·B4(37.8 ms)·B5(832.9 MB) 통과, **B1 11/14·B2 12/14 초과**와 단계 분해(cooked 로드의 embedded texture PNG 디코드 93~98%, 저작 트랜잭션의 corpus 재스캔·stage 재판독·source 재해시), B6 기준선 기록(메시 업로드 10개/1,075 KB·VRAM 1,145 MB), corpus 재임포트 14/14(신원 324개 보존·generation만 상승)로 CEMC v5 전환 완료, cutover 보류와 후보 4종 |
| 2026-09-02 | 신설(MBC0). 동결 표면 15종·하드 계약 3종·stash 처리 3종, corpus 14/참조 28 분류(모델 참조 8·subasset 참조 0·고아 11), Prim sidecar 손상 원인 경로, Release 성능 기준선과 예산 B1~B6 |
