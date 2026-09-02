# 모델 자산 빅뱅 전환 계획 (PHASE 3.75)

**신설 2026-09-02 · 예상 공수 60 개발일 · 상태: 진행 중 — MBC0·MBC1·MBC2 완료(9/60일, 2026-09-02)**

이 계획은 기존 `experiment` 배선을 완성하거나 legacy 경로와 대조 운용하는 계획이 아니다.
모델 자산의 신원·저장·로딩·런타임 소비를 새 계약으로 한 번에 교체하고, 전환 완료 뒤
legacy `Model` 계층·Assimp 폴백·역브리지·A/B 스위치를 제품에서 제거하는 **단방향
cutover** 계획이다.

PHASE 4는 이 계획의 완료 산출물만 입력으로 받는다. 구
[`ModelImportPipelinePlan.md`](ModelImportPipelinePlan.md)의 I/V 트랙은 구현 이력과 실패
증거로만 보존하며, 더 이상 PHASE 4의 활성 범위·공수·진행률에 포함하지 않는다.

---

## 0. 결정과 정본 경계

### 0.1 확정 결정

1. **기존 GUID를 제품 신원으로 보존·매핑·재활용하지 않는다.** 전체 모델 자산 corpus에
   새 identity epoch를 적용하고 모델·메시·재질·임베디드 텍스처·스켈레톤·애니메이션
   신원을 다시 발급한다.
2. **UUIDv8 + SHA-256 프로필을 정식 채택한다.** UUIDv4 또는 UUIDv5를 최종 모델 자산
   신원 규약으로 사용하지 않는다.
3. **기존 sidecar를 부분 수정하지 않는다.** schema v2 sidecar와 모든 subasset 신원을
   하나의 writer가 완성한 뒤 원자 게시한다.
4. **A/B 런타임·silent fallback을 제거한다.** 새 경로 실패는 legacy 로더로 우회하지 않고
   해당 generation 게시를 거부한다.
5. **`experiment`에서 legacy 객체를 재생성하지 않는다.** renderer·scene·animation·editor가
   새 모델 aggregate를 직접 소비한다.
6. **제품 cutover는 한 번이다.** 구현은 빌드 가능한 내부 슬라이스로 나누되, 제품에 두
   모델 체계를 장기 공존시키거나 중간 슬라이스를 부분 공개하지 않는다.

### 0.2 정본

| 범위 | 정본 |
|---|---|
| PHASE 3.75 순서·공수·완료 기준 | 이 문서 |
| UUIDv8 byte-level identity profile | 이 문서 §2 |
| sidecar schema v2·writer transaction | 이 문서 §3 |
| 런타임 aggregate·소비자 cutover | 이 문서 §4~§6 |
| MBC0 기준선(동결 표면·corpus/참조 분류·Release 성능·예산 B1~B6) | [`ModelAssetBigBangCutoverBaseline.md`](ModelAssetBigBangCutoverBaseline.md) — 다시 뜨지 않는 archive |
| 대시보드 표시 | `RefactoringPlanDashboard.html` PHASE 3.75 — 이 문서의 파생 표시 |
| 구 I/V 구현·측정 이력 | `ModelImportPipelinePlan.md` — **역사 자료이며 활성 계획이 아님** |

---

## 1. 왜 새 페이즈가 필요한가

현재 결함은 두 개의 파일에 국한되지 않는다.

- `SU_Mythic.glb`가 그려지지 않는 현상은 메시가 가진 실제 속성 조합과 PSO/셰이더
  변형 키가 한 계약에서 나오지 않는 문제다. COLOR와 SKIN이 함께 있는 stride 84 경로를
  `core|color|skin`으로 구분하지 못하면 입력 레이아웃 또는 `VSIn`이 다른 바이트를 읽는다.
- `Gunner_F_Mythic.glb`의 재질이 비는 현상은 모델이 완전히 검증되기 전에 임베디드
  텍스처를 전역 등록하고, 역직렬화 순서로 등록부를 미리 데우는 부분 게시 문제다.
- Prim 8개 sidecar에서 최상위 GUID와 `subAssets`가 분리 변경된 것은 모델 신원 발급과
  subasset refresh가 서로 다른 writer라는 증거다.
- experiment 실패 뒤 legacy `Model::LoadModel*`로 돌아가는 동안 animation tick만 먼저
  제거하면 메시만 보이고 애니메이션이 멈춘 혼합 상태가 정상처럼 게시된다.
- `BuildLegacyModelFromExperiment`, legacy cache, `m_hashingMesh` 기반 binding은 새
  파이프라인을 정본이 아니라 legacy 객체 생산기로 만든다.

따라서 sidecar 8개를 옛 GUID로 되돌리거나 fallback을 한 겹 더 붙이는 것은 목표가 아니다.
필요한 것은 **새 신원 epoch, 원자적 authoring transaction, 원자적 runtime generation,
직접 소비자, legacy 0건 게이트**를 한 경계로 묶는 것이다.

---

## 2. `ce.uuidv8.sha256.v1` 신원 프로필

### 2.1 출력 형식

- 외부 표현: RFC UUID canonical text, 소문자 36자.
- 내부 표현: 16-byte network order.
- 해시: SHA-256.
- UUID version: 8.
- UUID variant: RFC 4122/9562 variant `10xx`.
- 유효 digest bit: version·variant 고정 뒤 122bit.

UUIDv8은 해시 알고리즘을 지정하지 않는다. 그러므로 아래 프로필 전체가 식별자의 계약이며
`UUIDv8`이라는 이름만으로 호환된다고 판정하지 않는다.

### 2.2 입력 byte 계약

```
IdentityInput :=
  Bytes("ce.uuidv8.sha256.v1") || 0x00 ||
  U32BE(len(domain))    || UTF8_NFC(domain) ||
  U32BE(len(namespace)) || namespaceBytes  ||
  U32BE(len(kind))      || UTF8_NFC(kind)   ||
  U32BE(len(stableKey)) || UTF8_NFC(stableKey)

digest := SHA256(IdentityInput)
uuidBytes := digest[0..15]
uuidBytes[6] := (uuidBytes[6] & 0x0f) | 0x80
uuidBytes[8] := (uuidBytes[8] & 0x3f) | 0x80
```

규칙:

- 문자열은 UTF-8 NFC이며 NUL 종단자를 포함하지 않는다.
- 길이는 변환 후 byte 수를 unsigned 32-bit big-endian으로 기록한다.
- 경로가 stable key의 일부일 때만 프로젝트 상대 경로, `/` separator, `.`·`..` 제거,
  asset database가 확정한 대소문자를 사용한다. 임의 lower-case는 금지한다.
- UUID namespace를 넣을 때는 문자열이 아니라 16-byte network-order 값을 넣는다.
- epoch seed를 넣을 때는 sidecar header에 저장된 32-byte 값을 그대로 넣는다.
- 프로필·정규화·truncation·bit setting 중 하나라도 달라지면 다른 identity profile이다.

### 2.3 계층 신원

프로젝트는 cutover 시 CSPRNG로 새 256-bit `identityEpochSeed`를 한 번 발급한다. 이것은
legacy GUID가 아니며 제품 AssetId로 노출하지 않는다.

```
ModelId     = V8(domain="model", namespace=identityEpochSeed,
                 kind="model", stableKey=modelAuthoringKey)
MeshId      = V8(domain="subasset", namespace=ModelId,
                 kind="mesh", stableKey=meshStableKey)
MaterialId  = V8(domain="subasset", namespace=ModelId,
                 kind="material", stableKey=materialStableKey)
TextureId   = V8(domain="subasset", namespace=ModelId,
                 kind="texture", stableKey=textureStableKey)
SkeletonId  = V8(domain="subasset", namespace=ModelId,
                 kind="skeleton", stableKey=skeletonStableKey)
AnimationId = V8(domain="subasset", namespace=ModelId,
                 kind="animation", stableKey=animationStableKey)
```

`modelAuthoringKey`는 migration 시 새로 만드는 256-bit 불변 authoring key 또는 exporter가
제공한 검증 가능한 persistent ID다. 기존 GUID를 key나 namespace로 사용하지 않는다.

subasset stable key 우선순위:

1. `extras.creatorEngineId`처럼 exporter가 보존하는 명시적 persistent ID.
2. 원본 DCC/exporter persistent object ID.
3. 타입·계층·명시 이름으로 만든 검증 가능한 semantic key.
4. schema v2에 한 번 기록한 새 immutable authoring key.

배열 ordinal만 쓴 `gltf/material/0`, 표시 이름만 쓴 key, 파일명 기반 추측은 금지한다.
동일 kind 안에서 key가 중복되거나 안정성을 증명할 수 없으면 import를 실패시킨다.

### 2.4 충돌과 재현성 게이트

- `(profile, epoch, modelAuthoringKey, kind, stableKey) ↔ UUID`는 전 corpus에서 bijection이어야 한다.
- writer는 UUID 충돌뿐 아니라 canonical tuple 중복도 검사한다.
- 같은 입력은 Windows 개발기·AssetCooker·Player staging에서 같은 UUID를 만들어야 한다.
- SHA-256이 충돌 불가능하다고 가정하지 않는다. 충돌은 게시 거부와 진단 artifact 대상이다.
- 기존 pseudo UUIDv5 구현처럼 SHA-1 결과에 v4 nibble을 쓰는 코드는 0건이어야 한다.

---

## 3. sidecar schema v2와 원자 writer

### 3.1 최소 schema

```yaml
schemaVersion: 2
identityProfile: ce.uuidv8.sha256.v1
identityEpoch: <epoch name>
identityEpochSeed: <32-byte hex; project identity header에서 참조 가능>
authoringKey: <new immutable key>
assetId: <uuidv8>
generation: <monotonic integer>
sourceFingerprint: <sha256>
subAssets:
  - kind: material
    stableKey: <typed stable key>
    assetId: <uuidv8>
```

실제 저장 위치는 project identity header와 sidecar 사이 중복을 피하도록 구현 시 확정한다.
단, `identityProfile`, `identityEpoch`, `authoringKey`, `assetId`, `generation`, 완전한
`subAssets` closure는 반드시 재구성 가능해야 한다.

**MBC2에서 확정(2026-09-02).**

- epoch header는 `ProjectSetting/AssetIdentity.asset` 하나(schemaVersion 1, identityProfile,
  identityEpoch, identityEpochSeed 64hex, createdAt). ProjectSetting은 pak에 들어가므로 Player가
  §4 load 1단계를 같은 파일로 검증한다. sidecar는 seed를 복제하지 않고 `identityEpoch` 이름만
  들어 header와 대조한다.
- sidecar v2는 최상위 `schemaVersion: 2` 아래 `identityProfile`·`identityEpoch`·`authoringKey`·
  `assetId`·`generation`·`sourceFingerprint(sha256:<64hex>)`·`subAssets[]{kind, stableKey,
  assetId, binding, name, fingerprint}`. legacy 최상위 `guid:`는 없다(있으면 reader가 거부).
  v1(`subAssets.schemaVersion: 1`)은 `LegacySchema`로 거부 — MBC4 offline migrator의 입력이다.
  `importSettings`·`ModelImporter` 등 다른 최상위 키는 writer가 보존한다.
- stable key 문법: `exporter:<id>` | `name:<NFC 이름>` | `authoring:<64hex>`. ordinal
  (`gltf/material/0`)·빈 값·비NFC·대문자 접두는 파서가 거부한다. 모델 `authoringKey`는
  `exporter:`·`authoring:`만(이름 없음).
- authoring key 재결합 규칙: 요소의 콘텐츠 지문(SHA-256 — 재질=속성+슬롯 텍스처 지문,
  텍스처=바이트, 메시=위치+인덱스, 스켈레톤=joint 이름+inverse bind, 애니메이션=타깃 이름+키)
  으로 prior key를 되찾는다. 같은 지문이 여럿이면 binding 순으로 짝짓는다(바이트 동일 요소는
  교환 불가시). prior에 없는 지문 → 새 key, 현재에 없는 prior → 은퇴(경고), **둘이 동시** →
  `AuthoringRebindAmbiguous` 오류로 import 실패(§2.3 마지막 문장의 구현).
- 실측: corpus 14 모델 309 subasset 중 scene.glb(무명 재질 25·텍스처 69·메시 103)만 authoring,
  나머지 112건은 전부 semantic. exporter persistent ID는 0건이라 우선순위 1·2 경로는 필드만
  열어 두고(`StableKeyElement::persistentId`) 임포터가 채우는 것은 MBC3에서 extras 콜백으로.

### 3.2 단일 authoring transaction

`ModelAssetAuthoringTransaction` 하나가 다음 순서를 소유한다.

1. source를 임시 draft로 decode한다. 전역 catalog/cache는 변경하지 않는다.
2. stable key 전부를 만들고 중복·누락을 검사한다.
3. UUIDv8 closure를 계산하고 corpus registry와 충돌 검사한다.
4. geometry·material·texture·skeleton·animation 참조 closure를 검증한다.
5. schema v2 sidecar와 cooked artifact를 같은 temporary generation에 쓴다.
6. 임시 산출물을 다시 읽어 UUID 재계산·closure·source fingerprint를 검증한다.
7. sidecar, cooked artifact, catalog record를 원자 rename/publish한다.
8. 파일 watcher에는 최종 generation 하나만 보인다.

실패 시 임시 generation만 폐기한다. 최상위 GUID만 바뀌거나 `subAssets`가 사라진 sidecar,
모델 게시 전에 전역 texture registry만 데워진 상태는 존재할 수 없어야 한다.

### 3.3 writer 단일화

- `ModelIdentityRefresher`의 모델 신원 발급 책임을 제거한다.
- `CreateMetaLocked`, importer refresh, watcher refresh, AssetCooker가 별도 규칙으로 sidecar를
  쓰지 못하게 한다.
- source import·reimport·cook·rename은 같은 transaction API를 호출한다.
- writer 외부에서 model sidecar의 `assetId`·`subAssets`를 수정하는 호출은 정적 게이트로 막는다.

---

## 4. 런타임 정본: `ModelAssetGeneration`

런타임에는 다음 immutable aggregate 하나만 게시한다.

```
ModelAssetGeneration
 ├─ identity { ModelId, generation, sourceFingerprint }
 ├─ meshes[] { MeshId, vertexLayout, vertex/index storage, bounds }
 ├─ materials[] { MaterialId, property block, texture handles }
 ├─ textures[] { TextureId, decoded/upload descriptor }
 ├─ skeleton { SkeletonId, hierarchy, inverse bind }
 ├─ animations[] { AnimationId, tracks, events }
 └─ gpuDescriptors { backend-neutral immutable requests }
```

load 순서:

1. sidecar/cooked header의 profile·epoch·fingerprint를 검증한다.
2. CPU draft와 모든 내부 handle을 pending generation에 만든다.
3. embedded texture와 material 연결을 포함한 closure를 검증한다.
4. GPU upload request를 준비하고 양 backend 요구를 검증한다.
5. generation 전체를 한 번에 cache/catalog에 게시한다.

동일 AssetId reimport는 `try_emplace`로 옛 객체를 남기지 않고 generation을 교체한다. cache
key는 `{AssetId, generation}`이며 mesh binding은 `{ModelId, MeshId, generation}`이다.
legacy `Mesh::m_hashingMesh`를 신원으로 사용하지 않는다. retire는 모델·subasset·GPU
descriptor generation 전체를 함께 처리한다.

---

## 5. 직접 소비와 제거 경계

### 5.1 직접 소비자로 전환할 표면

| 소비자 | 최종 입력 |
|---|---|
| GBuffer·Forward·Shadow·Depth·Wireframe | `MeshId` + vertex attribute mask + immutable material snapshot |
| `MeshRenderer`·scene bridge | `ModelId`/`MeshId`/`MaterialId` generation handle |
| Animator·animation job | skeleton/animation handle와 pose output |
| Foliage·Collider·proxy | mesh bounds/storage handle, 필요한 경우 명시적 CPU view |
| Editor Inspector·preview | read-only generation snapshot과 authoring transaction command |
| Serialization·CLR | UUIDv8 AssetId와 typed subasset handle |
| AssetCooker·Player package | schema v2 + verified cooked generation |

최종 namespace는 `experiment`가 아니다. cutover 전용 내부 이름을 거쳐도 제품 게시 전에
`RenderEngine::Assets` 계층의 정식 타입으로 승격한다.

### 5.2 제품에서 제거할 것

- `BuildLegacyModelFromExperiment`와 legacy `Model/ModelNode/Mesh/Material/Skeleton/Bone/
  NodeAnimation` 복제.
- legacy `Models`·`Materials` cache와 `m_experimentMeshBindings` 병행 상태.
- `ModelSceneBridge`·`GetOrUpload`·Animator의 legacy fallback.
- Assimp 제품 importer와 vcpkg 제품 의존.
- `CREATOR_EXPERIMENT_VERTEX`, experiment on/off, parity/skip-success 분기.
- legacy `.asset` runtime reader와 제품 migration alias.
- 정상 제품 경로의 `[material.finalize]`, `[mesh.resolve]`, `[model.instantiate]`,
  `[anim.tick]` 무조건 출력.
- 상태를 바꾸는 `experiment.animlive`류 제품 진단 명령.

과거 포맷을 읽는 도구가 꼭 필요하면 제품 밖 일회성 offline migrator로만 둔다. migrator는
기존 GUID를 **입력 위치 찾기**에 일시 사용할 수 있지만 새 AssetId 계산·runtime alias에는
사용할 수 없다. migration 완료 게이트는 저장소 전체 old GUID 참조 0건이다.

---

## 6. 두 긴급 자산의 폐쇄 조건

### 6.1 `SU_Mythic.glb`

- PSO/permutation key에 전체 `vertexAttributeMask`를 넣는다.
- 최소 조합 `core`, `core|color`, `core|skin`, `core|color|skin`을 구별한다.
- `core|color|skin` stride 84, bone indices offset 64, bone weights offset 68을 layout table에서
  유도하며 하드코딩하지 않는다.
- GBuffer와 Shadow, DX12와 Vulkan 모두 같은 mask에서 input layout과 `VSIn`을 만든다.
- COLOR_0을 제거해 통과시키거나 skin을 legacy layout으로 우회하지 않는다.
- source load·cold cooked load·reimport 뒤 대표 프레임에서 geometry, color, skin pose가 모두
  유효해야 한다.

### 6.2 `Gunner_F_Mythic.glb`

- 새 epoch에서 model 1, material 2, embedded texture 6의 UUIDv8 closure를 sidecar에 기록한다.
- cold load에서 임베디드 texture decode → material property 연결 → model generation 게시가 한
  transaction이다.
- `MeshRenderer::OnDeserialized`가 재질 노드에서 model GUID를 미리 찾아 registry를 데우는
  순서 해킹을 제거한다.
- 이름 fallback, 이전 전역 registry 잔존, hot cache가 없어도 두 material의 texture property가
  완전해야 한다.
- reimport generation 교체 뒤 이전 texture generation이 재사용되지 않아야 한다.

---

## 7. 실행 슬라이스와 공수

공수는 1인 전담 엔지니어 개발일이다. 제품 공개는 `MBC11`에서 한 번만 하지만, 각
슬라이스는 cutover branch에서 독립적으로 빌드·검증 가능해야 한다.

| ID | 내용 | 선행 | 공수 | 상태 |
|---|---|---|---:|---|
| `MBC0` | cutover 계약·변경 동결·corpus/참조/성능 기준선 | — | 2 | **완료 2026-09-02** — `verify-mbc-cutover-freeze`(래칫 15 표면·하드 계약 3), `mbc0_corpus_baseline.json`, [기준선 문서](ModelAssetBigBangCutoverBaseline.md) |
| `MBC1` | `ce.uuidv8.sha256.v1` 구현·test vector·collision registry | MBC0 | 3 | **완료 2026-09-02** — `Engine/RenderEngine/Assets/AssetIdentityProfile·Registry`, `Utility_Framework/Sha256.h`, `assets.identity`(단정 184), `verify-asset-identity`(C++·Python·.NET 3중 유도, 벡터 15) |
| `MBC2` | schema v2·stable key·epoch header | MBC1 | 4 | **완료 2026-09-02** — `Assets/AssetIdentityEpoch`·`ModelStableKeys`·`ModelSidecarV2`, `assets.sidecar`(단정 83, corpus 14/14·subasset 309·충돌 0), `verify-asset-sidecar-v2` |
| `MBC3` | 원자 authoring writer·watcher/AssetCooker 단일화 | MBC2 | 5 | 미착수 |
| `MBC4` | 전 corpus 새 신원 발급·scene/prefab/material 참조 일회성 rewrite | MBC3 | 6 | 미착수 |
| `MBC5` | `ModelAssetGeneration`·generation cache·원자 publish/retire | MBC2 | 7 | 미착수 |
| `MBC6` | RHI/GBuffer/Forward/Shadow 직접 소비 + SU 전체 mask 조합 | MBC5 | 5 | 미착수 |
| `MBC7` | Scene/MeshRenderer/material 직접 소비 + Gunner cold-load closure | MBC3, MBC5 | 5 | 미착수 |
| `MBC8` | Animator/Foliage/Collider/Editor/Serialization/CLR 직접 소비 | MBC5 | 6 | 미착수 |
| `MBC9` | A/B·Assimp fallback·역브리지·legacy 타입/cache 제거 | MBC6~MBC8 | 5 | 미착수 |
| `MBC10` | 진단 read-only화·정적/변이/실자산 gate 완결 | MBC1, MBC9 | 5 | 미착수 |
| `MBC11` | 전체 build/runtime/performance 검증·단일 제품 cutover | MBC4, MBC9, MBC10 | 7 | 미착수 |
| **합계** | | | **60일** | |

병렬 가능 범위는 `MBC3`과 `MBC5`, `MBC6`~`MBC8`이다. 한 명이 순차 수행하면 60
개발일, 약 12 work-week다. 2명이 writer/runtime 축과 consumer 축을 나누되 동일 gate owner를
두면 8~10주가 현실적이다. 공식 일정은 **10주 목표 + 2주 여유**로 둔다.

---

## 8. 게이트

### 8.1 정적 identity·sidecar

- [ ] 모든 model sidecar `schemaVersion=2`, profile=`ce.uuidv8.sha256.v1`.
- [ ] UUID 재계산 불일치 0, canonical tuple 중복 0, UUID 충돌 0.
- [ ] `subAssets` closure 누락 0; 모델별 source decode 결과와 kind별 개수가 일치.
- [ ] legacy GUID 참조 0; legacy GUID→new GUID runtime map/alias 0.
- [ ] pseudo UUIDv5-as-v4 구현·호출 0.
- [ ] model sidecar writer가 `ModelAssetAuthoringTransaction` 한 곳뿐.

### 8.2 구조

- [ ] Assimp 제품 호출·include·package 의존 0.
- [ ] experiment/legacy 선택 플래그·off arm·fallback 0.
- [ ] `BuildLegacyModelFromExperiment`와 legacy 모델 객체 복제 0.
- [ ] `m_hashingMesh`·legacy `Models/Materials` cache 기반 binding 0.
- [ ] 모델·임베디드 텍스처·재질 부분 게시 가능 경로 0.
- [ ] 제품 소비자는 `ModelAssetGeneration` handle만 사용.

### 8.3 build·runtime

- [ ] AssetCooker, RenderTests, CreatorEditor Debug/Release 빌드.
- [ ] DX12와 Vulkan validation error 0.
- [ ] 전체 모델 corpus source load·cold cooked load·reimport 통과.
- [ ] `SU_Mythic.glb` geometry/color/skin pose가 GBuffer·Shadow 양 backend에서 통과.
- [ ] `Gunner_F_Mythic.glb` material 2/2, embedded texture 6/6이 cold load·reimport에서 통과.
- [ ] 실패 주입 단계별 catalog/cache/scene/render delta 0.
- [ ] scene save/reload·prefab·CLR round-trip 후 AssetId와 subasset handle 일치.

### 8.4 성능

legacy runtime과 live A/B 스위치를 두지 않는다. cutover 전에 고정한 archived baseline과 새
단일 경로를 별도 실행으로 비교하고, 제품에는 새 경로만 남긴다.

- [ ] Release cold load·warm load·reimport 시간 예산 충족.
- [ ] model CPU resident·vertex/index·texture staging·peak VRAM 예산 충족.
- [ ] frame CPU record와 GPU pass timing 비회귀.
- [ ] cache hit/miss·generation retire 수가 읽기 전용 snapshot으로 관측됨.

예산 수치는 `MBC0`에서 같은 장비·같은 asset corpus·같은 해상도로 고정한다. 기준을 못
맞추면 cutover를 연기하되 legacy fallback을 제품에 되살리지 않는다.

---

## 9. 완료 정의와 되돌리지 않는 선

PHASE 3.75 완료는 새 타입이 추가된 상태가 아니다. 아래가 모두 참이어야 한다.

1. 전체 corpus와 저장된 모든 scene/prefab/material 참조가 새 UUIDv8 epoch만 사용한다.
2. sidecar/cooked/catalog가 generation 단위로 원자 게시된다.
3. 모든 제품 소비자가 새 aggregate를 직접 사용한다.
4. SU와 Gunner 긴급 자산이 cold load·reimport·양 backend에서 통과한다.
5. Assimp, legacy 모델 타입/cache, 역브리지, A/B switch, silent fallback이 제품에서 0건이다.
6. 정적 검사, 빌드, runtime, 성능 검증을 각각 통과하고 결과 artifact를 보존한다.

`MBC11` cutover가 승인된 뒤에는 legacy GUID 복원, runtime mapping, Assimp fallback, experiment
on/off를 복구 경로로 사용하지 않는다. rollback은 이전 release 전체로 되돌리는 배포 결정이며,
새 release 안에서 두 자산 체계를 다시 병행시키는 코드 경로가 아니다.

---

## 10. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-02 | 신설. 구 PHASE 4 I/V experiment 배선을 PHASE 3.75 단방향 cutover로 대체. 기존 GUID 비승계, UUIDv8+SHA-256 profile, schema v2 원자 writer, `ModelAssetGeneration`, SU/Gunner 폐쇄 조건, legacy/A-B/Assimp 제거, 60일 공수와 게이트를 확정 |
| 2026-09-02 | **MBC0 완료.** 동결 래칫 게이트(표면 15·하드 계약 3), corpus 14/참조 28 분류(모델 참조 8·subasset 참조 **0**·고아 11), Prim sidecar 8개 손상의 원인 경로(워처 Delete 오독 → `CreateMetaLocked` 재발급) 실측, Release 기준선·예산 B1~B6 고정. 미커밋 폴백 덧대기(MeshRenderer 순서 해킹·`[material.finalize]`)와 손상 sidecar는 stash로 걷어냈다. **계측 공백**: 새 경로 cooked 읽기·frame CPU/GPU·peak VRAM은 CLI가 없어 MBC11 전에 세워야 한다 |
| 2026-09-02 | **MBC2 완료.** epoch header(`ProjectSetting/AssetIdentity.asset`, CSPRNG 256-bit), stable key 문법·규칙 엔진(semantic/authoring, 지문 재결합, 모호성=고아 prior+새 지문 동시), sidecar v2 코덱(왕복·다른 키 보존·legacy guid 제거·v1/ordinal 거부)·폐포 검증(재유도·registry bijection). 실자산 14 모델 임포트→배정→v2→폐포 통과, 전 corpus registry 309 충돌 0, 같은 입력 재배정 동일 신원. 첫 규칙은 scene.glb의 동일 지오메트리 무명 메시에서 변경 없는 재임포트를 거부했다 — 지문 그룹 안 binding 순 짝짓기로 정련. 디스크 쓰기 없음(원본 해시 전후 동일 게이트). §3.1에 확정 사항 기록 |
| 2026-09-02 | **MBC1 완료.** §2 바이트 계약을 `assets::DeriveIdentity`로 구현(헤더 온리 SHA-256, UTF-8 well-formed·NFC fail-closed, 길이 접두 U32BE, v8/variant bit), 계층 `DeriveModelId`/`DeriveSubAssetId`(legacy v4 namespace는 값에서 거부), `IdentityRegistry`(DuplicateTuple/UuidCollision/RecomputeMismatch, canonical tuple = 입력 바이트열). test vector 15건은 Python 독립 유도로 생성하고 .NET이 3차 검산. **변이 검증**: 길이 접두를 LE로 바꾼 제품 빌드에서 게이트 RED(단정 86 실패), 되돌리면 GREEN. §2.4의 pseudo v5-as-v4 0건은 MBC3가 writer를 교체할 때 래칫을 내린다(현재 접촉 2+1) |
