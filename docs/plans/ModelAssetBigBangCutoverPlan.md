# 모델 자산 빅뱅 전환 계획 (PHASE 3.75)

**신설 2026-09-02 · 예상 공수 60 개발일 · 상태: 진행 중 — MBC0~MBC8 완료(43/60일, 2026-09-03)**

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
authoringKey: <new immutable key>
assetId: <uuidv8>
generation: <monotonic integer>
sourceFingerprint: <sha256>
subAssets:
  - kind: material
    stableKey: <typed stable key>
    assetId: <uuidv8>
```

epoch seed는 `ProjectSetting/AssetIdentity.asset`에만 저장하며 sidecar는 `identityEpoch`로
그 header를 참조한다. `identityProfile`, `identityEpoch`, `authoringKey`, `assetId`,
`generation`, 완전한 `subAssets` closure는 반드시 재구성 가능해야 한다.

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
- MBC2 당시 실측은 corpus 14 모델 309 subasset이었다. MBC5 cold-load가 animation-only
  `Cha_Mon_5.fbx`의 유도 skeleton 1건이 inventory에서 빠진 것을 검출해 writer를 보완했으며,
  현재 정본은 310 subasset이다. scene.glb(무명 재질 25·텍스처 69·메시 103)만 authoring,
  나머지 기존 112건은 semantic이었다. 당시 exporter persistent ID는 0건이었다.
  MBC3에서 glTF `extras.creatorEngineId` 콜백을 열고 mesh/material/image/skin/animation의
  `StableKeyElement::persistentId`로 전달한다. ID가 없는 기존 corpus의 배정은 바뀌지 않는다.

**PHASE 17 병합 재검증(2026-09-02).** PHASE 17은 yaml-cpp를 source·manifest·runtime에서
0으로 만든다. 병합 전 MBC2의 `AssetIdentityEpoch`·`ModelSidecarV2`가 yaml-cpp를 직접
사용해 의미 충돌이 있었으므로 둘을 `Authoring::ParsedDocument`·`WriteDocument`로 이식했다.
UUIDv8 byte profile과 schema v2 출력 계약은 유지되고, `verify-yaml-cpp-retirement` 0건,
`verify-asset-identity` 184/184, `verify-asset-sidecar-v2` 85/85·corpus 14/14·subasset
310·registry 충돌 0으로 다시 닫았다. PHASE 17 D2의 UUIDv4 설계·수치는 전환 전 역사와
MBC4 입력 기준선일 뿐 최종 identity authority가 아니다.

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

**MBC3 완료(2026-09-02).** `Engine/RenderEngine/Assets/ModelAssetAuthoringTransaction`이
프로젝트 단위 cross-process mutex 아래 decode → stable key/NFC → UUIDv8 closure·corpus 충돌
검사 → CEMC/embedded texture/generation record·schema v2 sidecar staging → 전부 재판독 → generation
rename → canonical sidecar commit을 소유한다. `EditorAssetDatabase`의 생성·수정·이동·import와
`AssetCooker --author-model-asset`은 이 API만 호출한다. legacy `ModelIdentityRefresher`와
pseudo-v5-as-v4 구현은 삭제했고, UUIDv4 repair/apply 도구는 model 및 model sidecar를 거부한다.
`verify-model-authoring-transaction`은 Prim_Cube 복제본에서 generation 1→2 동안 model/subasset
UUIDv8 4개가 유지됨, 주입 실패 5단계의 sidecar/generation delta 0, corpus collision 게시 거부,
temporary 누수 0을 확인했다. Debug/Release x64 AssetCooker·CreatorEditor 빌드와 MBC1/MBC2
게이트(184/184, 85/85, corpus 14/14·subasset 310·충돌 0)를 통과했다. 실제 corpus 발급과 저장
참조 rewrite, project epoch header의 정식 생성은 MBC4 범위라 이 단계에서 원본 sidecar를 바꾸지 않았다.
따라서 MBC3 종료 시점의 legacy `verify-experiment-model-cook-all`은
`Models/scene.glb.meta`의 불완전한 v1 `subAssets`에서 RED였다. 이는 writer의 fixture 원자성
실패가 아니라 MBC4가 전 corpus를 schema v2로 발급하기 전에 닫아야 했던 진입 차단점이었다.

**MBC4 완료(2026-09-02).** `Tools/migration/Invoke-ModelAssetV8Cutover.ps1`이 MBC0
baseline의 legacy GUID를 입력 위치 확인에만 읽고, 임시 프로젝트에서 정식 AssetCooker로 새
epoch header를 CSPRNG 발급한 뒤 모델 14개를 전부 MBC3 writer에 통과시킨다. 새 sidecar와
generation, 저장 참조 rewrite를 전부 검증한 뒤 generation → sidecar → document → epoch header
순서로 게시하며 실패하면 원본 sidecar/document와 generation을 되돌린다. old GUID→new UUIDv8
표는 프로세스 메모리 밖으로 게시하지 않는다.

MBC4 최초 적용 결과는 model 14, subasset 309, UUIDv8 identity 323, 최초 generation 14였다.
MBC5가 animation-only 유도 skeleton 누락을 닫아 현재 정본은 subasset 310, identity 324이며
모델별 canonical current generation 수는 14다.
legacy model/subasset identity 68개 중 저장 참조가 있던 `FT_Primitives.creator`의 model 참조
8개만 새 ModelId로 치환됐고, old identity 저장 참조는 0이다. baseline의 subasset 참조는
실자산 0건이므로 material/texture 치환은 합성 fixture 1건으로 검증했다. 이미 고아였던
`Test1`/`Test2`의 미해석 GUID 11개는 모델 신원으로 오인해 바꾸지 않았다. `scene.glb`를 포함한
기존 v1/불완전 sidecar 14개는 모두 schema v2가 됐고, `Gunner_F_Mythic`의 material 2·embedded
texture 6도 같은 closure에 포함됐다.

게이트는 corpus 실패 주입 뒤 snapshot 복구, 기존 epoch overwrite 거부, source SHA-256 불변,
generation/canonical sidecar 일치, old 참조 0을 확인한다. PHASE 17 Strict는 이제 비모델 UUIDv4
212개와 model UUIDv8 14개/subasset UUIDv8 310개를 함께 검사해 `identityReady=true`다. 구
`verify-experiment-model-cook-all` 진입점은 CI 호출 호환만 유지하고 legacy v1 cook 대신 MBC4
corpus generation 폐포와 MBC3 원자 authoring을 검증한다. `Dynamic_CPP/Library` generation은
재생성 가능한 로컬 산출물이고, `ProjectSetting/AssetIdentity.asset`만 epoch 정본으로 추적한다.

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

**MBC5 완료(2026-09-02).** `Assets/ModelAssetGeneration`이 schema v2 canonical sidecar,
generation record, CEMC와 embedded texture의 SHA-256을 모두 검증한 뒤 node·mesh·material·
decoded texture·skeleton·animation·animator·backend-neutral upload descriptor를 immutable
snapshot 하나로 만든다. `ModelAssetGenerationCache`는 `{ModelId,generation}`을 주소로 쓰고
asset별 current generation을 원자 교체하며 stale/same-generation collision을 거부한다.
`{ModelId,MeshId,generation}` typed handle, read-only 통계, aggregate retire와 외부 `shared_ptr`
snapshot 수명도 함께 닫았다. `DataSystem` startup catalog는 schema v2 `assetId`를 읽고 UUIDv8
모델 load/reload/remove/finalize에서 publish/retire한다. 다만 제품 소비자의 남은 legacy adapter는
MBC7~MBC9 범위이므로 이 단계의 완료가 legacy 제거 완료를 뜻하지 않는다.

실자산 cold-load에서 `Cha_Mon_5.fbx`의 animation-only CEMC에는 유도 skeleton과 clip 2개가
있지만 sidecar inventory에는 skeleton이 없던 기존 부분 게시가 검출됐다. writer가 애니메이션
target node 조상 폐포를 SHA-256 지문화해 신규 skeleton UUIDv8을 한 번 발급하도록 수정하고,
sidecar↔ModelDraft 수 검산을 게시 전 필수 조건으로 추가했다. 현재 corpus는 model 14/14,
mesh 130, material 52, embedded texture 96, skeleton 4, animation 28, upload descriptor 356이며
subasset 310/identity 324다. `verify-model-asset-generation`은 generation 1→2 교체, model·sidecar·
record·texture 변조 4종의 게시 전 거부, 실패 뒤 current 불변, retire 2회를 Debug/Release에서
통과했다. identity/sidecar/MBC3/MBC4/freeze 게이트와 Debug/Release x64 AssetCooker·
CreatorEditor 빌드도 통과했다.

**MBC6 완료(2026-09-02).** `Assets/ModelVertexLayout`을 제품 정본으로 승격하고
`core`, `core|color`, `core|skin`, `core|color|skin` 네 조합을 전체
`vertexAttributeMask` PSO/permutation key로 분리했다. `ModelVertexInput`은 같은 표에서
DX12/Vulkan input element와 shader axis를 만들며, pass가 읽는 속성 집합은 전체 buffer layout과
별도로 받되 모든 오프셋은 원본 mask에서 계산한다. 따라서 depth-only Shadow도 COLOR가 사이에 낀
SU skin 오프셋을 잃지 않으면서 Vulkan unused-input 경고를 만들지 않는다.

`RHIModelMeshView`는 `{ModelId,MeshId,generation}`과 immutable vertex/index descriptor를 검증해
양 backend cache의 정확한 typed key로 올린다. GBuffer·Forward·Shadow는 이 view를 최우선으로
소비하고, Forward는 t12 bone palette와 instance별 bone offset까지 연결해 투명 skin pose를 더는
무시하지 않는다. SU closure는 mask 247, stride 84, bone indices 64, bone weights 68이며 COLOR를
제거하거나 legacy vertex upload로 우회하지 않았다. `verify-model-render-wiring`은 CPU layout/PSO
4종과 corpus 14/14를 검사한 뒤 GBuffer가 DX12/Vulkan typed generation upload를 각각 정확히
1회 수행했는지, DX12 skinning 및 Vulkan Shadow·GBuffer·Forward가 실제 실행되는지, validation이
0건인지를 요구하며 Debug/Release에서 통과했다. Scene/MeshRenderer가 typed view를
직접 채우는 전환은 MBC7, 남은 legacy upload arm의 물리 삭제는 MBC9 범위다.

**MBC7 완료(2026-09-03).** `MeshRenderer`가 모델 `ModelAssetGeneration`을 붙들고
(`m_modelGeneration`·`m_modelMeshIndex`, 영속 `m_meshAssetId` = UUIDv8 MeshId) 프록시
→ drawPool이 `BuildRHIModelMeshView`로 typed 뷰를 채운다. 그래서 MBC6이 비워 둔
`EnhancedDrawItem::modelMeshView`가 실씬에서 처음 채워졌고, 지오메트리 키는
`{ModelId, MeshId, generation}` 해시다. `Model::LoadModelToScene`은 generation 직행
인스턴스화(`GenerateSceneObjectHierarchyFromGeneration`)가 정본이고 experiment → legacy
재귀는 폴백이다. 재질은 generation의 immutable material에서 시공하며, embedded texture
owner는 `DataSystem::ResolveModelGenerationTexture`({TextureId, generation} 캐시,
generation 단위 retire)가 closure에서 만든다 — 전역 임베디드 등록부·이름 폴백·로드
순서·hot cache에 기대지 않는다. 저작 sealing(`BuildSealSourceFromAuthored`)과 M2
resolver도 같은 closure 훅(`resolveEmbeddedTexture`)을 첫 축으로 쓴다.

실측이 고친 결함 셋. ① 저장 시 `SynchronizeLegacyMaterialProperties`가 런타임 텍스처
**이름**으로 `Materials\*.png`(legacy Assimp 추출물)의 v4 GUID를 되살려 closure의 UUIDv8
신원을 덮어썼다 — UUIDv8은 이름에 지지 않는다. ② 재질 저작 코덕의 reader가 UUIDv4만
받아 writer가 이미 적던 UUIDv8 재질 assetId·texture GUID를 통째로 거부했다(저장 씬의
모델 재질이 typed 기본값으로 비었다) — 혼합 계약(PHASE 17 Strict)대로 v8을 받는다.
③ experiment 소스 로드의 v1 sidecar 리더가 schema v2를 거부해 legacy 재질이 embedded
GUID를 잃고 있었다 — v2 subAssets(binding = sourceKey)로 같은 표를 채운다(MBC9까지의
전환기 보강). 가시 결과: `verify-model-scene-consumption`이 FT_Primitives+Gunner 씬에서
renderer 10/10 typed, Gunner embedded 6/6 closure(등록부 출처 0), 콜드 프로세스 재로드
동일, reimport 뒤 이전 texture generation 재사용 0(retire 6), 실GPU 업로드 10/10 typed를
Debug에서 통과했다. `[mesh.resolve]`·`[model.instantiate]`에 `generation` 축이 생겨
`verify-experiment-vertex-live`·`verify-editor-drop-animation`은 typed 축을 받아들이도록
갱신했고(legacy 0은 그대로), dx12.scene 업로드 요약에 `generation` 계수를 더했다.
MBC3~MBC6 게이트 4종과 함께 run-all에 편입했다. Animator·Foliage·Collider·Editor의 typed
전환은 MBC8, legacy `m_Mesh`·experiment 병행 핸들·등록부의 물리 삭제는 MBC9 범위다.

**MBC8 완료(2026-09-03).** Animator가 `m_Motion`(ModelId)으로 `ModelAssetGeneration`을
붙들고(`m_modelGeneration`) 재생 틱·본 해석·신원·클립 열거·AvatarMask 생성·루프 판정이
typed skeleton/animation을 첫 축으로 쓴다(`AnimatorDataPath` = Generation → Experiment →
Legacy). 재생 산술은 `Assets/ModelAnimationSampler`(typed 트랙 샘플러·유니크 키 시각 계량)에
있고, `AnimationJob`의 틱 본문은 데이터 뷰 템플릿 하나(`GenerationPoseSource` /
`ExperimentPoseSource`)로 합쳐 두 판이 갈릴 자리를 없앴다 — typed 샘플러는 experiment 골든
`poseDigest=8042DC1C`를 **비트 동일**하게 재현했고, A/B 스위치를 끈 프로세스에서도 같은
값을 낸다(재생의 스위치 의존 은퇴). skeleton 신원은 {ModelId, SkeletonId, generation}
해시라 legacy m_serial·experiment Generation 번호 공간과 겹치지 않는다. Foliage는
`FoliageType::m_modelGeneration`을 이름 → ModelId → generation으로 잇고 DrawSource →
drawPool이 typed 뷰를 채우며 재질 embedded texture를 같은 closure에서 푼다. 에디터
인스펙터는 typed generation read-only 블록(ModelId·MeshId·generation·epoch·정점/인덱스)을
보인다. cook의 씬 참조 스캔·재질 texture GUID 파서는 혼합 신원 계약(v4 비모델 + v8
모델/subasset)을 받는다 — MBC7 저장 씬이 `scene.reference`에서 거부되던 자리다.

실측 두 가지. ① Collider는 지오메트리를 소비하지 않는다 — `PhysicsManager::AddCollider(Mesh)`가
`HasComponent<MeshRenderer>`만 보고 convex cook은 정점 nullptr(PHASE 19 소관, 살아 있는
호출자 0). typed CPU view는 필요할 때 만든다(YAGNI). ② CLR 재질 API는 entity 핸들 + 이름
문자열만 넘기고 모델/메시 신원 표면이 없다 — typed bind 뒤 `m_Material`·`MaterialInstance`가
유효한 것으로 계약이 닫힌다(MBC7 게이트가 이미 잰다). 진단(`boneresolve`·`animmask`·
`editorsurface`·`animtick`·`animlive`·`foliage verify`)에 `generation` 축을 더하고 후보 선별을
legacy Skeleton 존재가 아니라 창구(`GetSkeletonSerial`)로 바꿨다. 신설 게이트
`verify-model-typed-consumers`는 스위치 off 프로세스에서 typed 전량(재생 골든·본 해석 N/N·
마스크 구조·클립 열거·Foliage 뷰 N/N)을 요구하고, vertex-live의 8/8b/9/9b/11b·off
4k/4m/4m2/4n을 typed 축으로 갱신했다. 남는 것: legacy `m_Skeleton` 대입(bridge·
OnDeserialized)·experiment 병행 핸들·`m_hashingMesh` Foliage 조회·`BuildLegacySkeleton`은
MBC9가 걷고, ModelCookProducer의 v1 sidecar 리더(cooked-catalog RED)는 MBC11 몫이다.

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
| `MBC2` | schema v2·stable key·epoch header | MBC1 | 4 | **완료 2026-09-02, PHASE 17 병합 후 재검증** — `Assets/AssetIdentityEpoch`·`ModelStableKeys`·`ModelSidecarV2`를 ryml 저작 문서 경계에 접합, `assets.sidecar`(단정 85, corpus 14/14·subasset 310·충돌 0), `verify-asset-sidecar-v2`, yaml-cpp 은퇴 0건 |
| `MBC3` | 원자 authoring writer·watcher/AssetCooker 단일화 | MBC2 | 5 | **완료 2026-09-02** — project mutex 기반 `ModelAssetAuthoringTransaction`, Editor create/modify/move/import·`AssetCooker --author-model-asset` 단일 진입, glTF exporter ID 수집, legacy refresher 삭제, 실패 주입 5단계·collision 1건 원자성 게이트, Debug/Release 빌드 |
| `MBC4` | 전 corpus 새 신원 발급·scene/prefab/material 참조 일회성 rewrite | MBC3 | 6 | **완료 2026-09-02** — epoch header CSPRNG 발급, model 14/subasset 310/identity 324(MBC5 animation-derived skeleton 보정 포함), current generation 14, 저장 참조 8 rewrite, old 참조 0, rollback·subasset 합성 gate |
| `MBC5` | `ModelAssetGeneration`·generation cache·원자 publish/retire | MBC2 | 7 | **완료 2026-09-02** — immutable aggregate·typed generation handle, SHA-256 closure load, replace/stale/collision/retire cache, DataSystem publish/retire, 변조 4종 fail-closed, corpus 14/14·mesh 130/material 52/texture 96/skeleton 4/animation 28/descriptor 356, Debug/Release gate |
| `MBC6` | RHI/GBuffer/Forward/Shadow 직접 소비 + SU 전체 mask 조합 | MBC5 | 5 | **완료 2026-09-02** — canonical 4-mask layout/PSO, typed `{ModelId,MeshId,generation}` DX12/Vulkan cache와 실제 upload 1/1, Forward skin palette, SU mask 247·84B·64/68, Debug/Release 실GPU gate·Vulkan validation 0 |
| `MBC7` | Scene/MeshRenderer/material 직접 소비 + Gunner cold-load closure | MBC3, MBC5 | 5 | **완료 2026-09-03** — MeshRenderer generation handle·영속 MeshId, 프록시→drawPool typed 뷰, generation 직행 인스턴스화, `ResolveModelGenerationTexture` closure 캐시(generation retire), 코덕 UUIDv8 수용·이름 역해석 차단, `verify-model-scene-consumption`(10/10 typed·Gunner 6/6·reimport 재사용 0·실GPU 10/10) |
| `MBC8` | Animator/Foliage/Collider/Editor/Serialization/CLR 직접 소비 | MBC5 | 6 | **완료 2026-09-03** — Animator `m_modelGeneration`·`AnimatorDataPath`, `Assets/ModelAnimationSampler`(골든 8042DC1C 비트 동일), 틱 뷰 템플릿 단일화, Foliage typed 뷰, 인스펙터 read-only 블록, cook 참조 스캔 v8 수용, `verify-model-typed-consumers`(스위치 off typed 전량) |
| `MBC9` | A/B·Assimp fallback·역브리지·legacy 타입/cache 제거 | MBC6~MBC8 | 5 | **완료 2026-09-03** — legacy `Model/ModelLoader/Skeleton/AnimatorData/Animation/ModelAssetFormat`·`ExperimentModelMigration`(역브리지·`LoadModelViaExperiment`·A/B 스위치)·`ModelSceneBridge`·Assimp include/vcpkg port 물리 삭제, `RHIExperimentVertexView`/`GetOrUploadExperiment`/experiment 계수·컴포넌트 병행 핸들(`m_experimentModel`·`m_Mesh`·`m_Skeleton`)·전역 임베디드 등록부·`Models` cache 제거, `ModelSceneInstantiation`(generation 직행)·`LoadModelAssetGenerationByPath`/`FindModelAssetGenerationByStem`/`ReadModelCreateMeshCollider`·`BoneRegion.h`(MAX_BONES) 신설, dx12/vulkan selftest·에디터 드롭·진단·transformbulk 프로브 typed 전환, 동결 래칫 제거 표면 0 재기준, `verify-experiment-vertex-live`·`phase17-local-model-corpus` 은퇴 |
| `MBC10` | 진단 read-only화·정적/변이/실자산 gate 완결 | MBC1, MBC9 | 5 | **완료 2026-09-03** — 제품 경로의 무조건 stdout 토큰(`[mesh.resolve]`·`[model.instantiate]`·`[anim.tick]`) 제거 → `ModelConsumptionDiagnostics` 원자 계수 + 읽기 전용 `assets.modeldiag`, `experiment.animlive`는 publish를 부르지 않고 제품 barrier의 마지막 메트릭 스냅샷(`Scene::TryGetLastAnimatorPoseMetrics`)을 읽음, 동결 게이트에 §5.2/§8.2 하드 계약(제거 표면 하드 0·`m_hashingMesh` 절차 지오메트리 허용목록·제품 소비자 experiment 객체 0·generation 게시 진입점 DataSystem 하나·animlive 읽기 전용) 추가, 게이트 5종을 스냅샷 파싱으로 전환 |
| `MBC11` | 전체 build/runtime/performance 검증·단일 제품 cutover | MBC4, MBC9, MBC10 | 7 | **검증 완료·cutover 보류 2026-09-03** — cook 경로 단일화(`ModelGenerationExportProducer`가 게시된 generation을 검증해 `Derived/Models/<xx>/<id>/<gen>/`로 내보내고 재질·텍스처·메시 subasset을 manifest에 등록, `ModelCookProducer` 삭제, `DataSystem::LoadModelAssetGeneration`이 cooked catalog → Library 순으로 해석·계수), 계측 공백 폐쇄(`assets.modelbench` author/cooked + `phases` 단계 분해 + peak WS/VRAM, `assets.modeldiag`의 generation 출처 계수), `verify-model-cutover-budget`(Release, run-all 배선). §8.4 실측: B3·B4·B5·B6 통과/기록, **B1 10/14·B2 12/14 초과**(원인: cooked 텍스처 재디코드·저작 트랜잭션 고정 비용) — [기준선 §4.5](ModelAssetBigBangCutoverBaseline.md). §8.4 규칙대로 cutover는 보류한다 |
| **합계** | | | **60일** | |

병렬 가능 범위는 `MBC3`과 `MBC5`, `MBC6`~`MBC8`이다. 한 명이 순차 수행하면 60
개발일, 약 12 work-week다. 2명이 writer/runtime 축과 consumer 축을 나누되 동일 gate owner를
두면 8~10주가 현실적이다. 공식 일정은 **10주 목표 + 2주 여유**로 둔다.

---

## 8. 게이트

### 8.1 정적 identity·sidecar

- [x] 모든 model sidecar `schemaVersion=2`, profile=`ce.uuidv8.sha256.v1`.
- [x] UUID 재계산 불일치 0, canonical tuple 중복 0, UUID 충돌 0.
- [x] `subAssets` closure 누락 0; 모델별 source decode 결과와 kind별 개수가 일치.
- [x] legacy GUID 참조 0; legacy GUID→new GUID runtime map/alias 0.
- [x] pseudo UUIDv5-as-v4 구현·호출 0.
- [x] model sidecar writer가 `ModelAssetAuthoringTransaction` 한 곳뿐.

### 8.2 구조 — `verify-mbc-cutover-freeze` 하드 계약(MBC10)

- [x] Assimp 제품 호출·include·package 의존 0 (`assimp.include`·vcpkg port 하드 0).
- [x] experiment/legacy 선택 플래그·off arm·fallback 0 (`CREATOR_EXPERIMENT_VERTEX`·`LoadModelViaExperiment` 하드 0).
- [x] `BuildLegacyModelFromExperiment`와 legacy 모델 객체 복제 0 (하드 0, 타입 파일 부재 — typed-consumers 정적).
- [x] `m_hashingMesh`·legacy `Models/Materials` cache 기반 binding 0 — 모델 경로 0. `m_hashingMesh`는 절차 지오메트리(스프라이트 쿼드·지형·기즈모) 캐시 키로만 허용목록 10파일에 남고 밖에 나타나면 실패.
- [x] 모델·임베디드 텍스처·재질 부분 게시 가능 경로 0 — generation 게시 진입점 `m_modelAssetGenerations.Publish`는 DataSystem 하나(정적), 게시 전 검증 실패는 전역 delta 0(`verify-model-asset-generation` 변조 4종).
- [x] 제품 소비자는 `ModelAssetGeneration` handle만 사용 — SceneRuntime/Render/PrimitiveRenderProxy/EngineGUIWindow에서 `experiment::Model`·`TryGetMesh` 0(정적).
- [x] 정상 제품 경로의 무조건 진단 출력 0·상태 변경 진단 명령 0 — `[material.finalize]`/`[mesh.resolve]`/`[model.instantiate]`/`[anim.tick]` 하드 0, `experiment.animlive`는 `PublishAnimatorPose`를 부르지 않는다(정적). 관측은 `assets.modeldiag`(원자 계수 스냅샷)·`Scene::TryGetLastAnimatorPoseMetrics`.

### 8.3 build·runtime

- [x] AssetCooker, RenderTests, CreatorEditor Debug/Release 빌드.
- [x] DX12와 Vulkan validation error 0 (`verify-model-render-wiring` — Vulkan validation 0건 단정).
- [x] 전체 모델 corpus source load·cold cooked load 통과 (`verify-model-asset-generation` corpus 14/14 cold-load closure); reimport는 Gunner(`assets.scenemodel reload`)로 대표 — 전 corpus reimport 시간 예산은 MBC11 §8.4.
- [x] `SU_Mythic.glb` geometry/color/skin pose가 GBuffer·Shadow 양 backend에서 통과 (`verify-model-render-wiring`).
- [x] `Gunner_F_Mythic.glb` material 2/2, embedded texture 6/6이 cold load·reimport에서 통과 (`verify-model-scene-consumption`).
- [x] 실패 주입 단계별 catalog/cache/scene/render delta 0 — generation 변조 4종 게시 전 거부·cache 무변화(`verify-model-asset-generation`); scene/render delta는 게시가 거부되면 소비자가 붙들 handle이 없어 구조적으로 0(`unbound=0` 단정과 함께).
- [x] scene save/reload·prefab·CLR round-trip 후 AssetId와 subasset handle 일치 (`verify-model-scene-consumption` 저장 씬 UUIDv8 `m_meshAssetId`·콜드 재해석, `verify-scene/prefab-authoring-corpus`; CLR은 이름 API뿐이라 handle 이관 대상 없음 — MBC8 실측).

### 8.4 성능

legacy runtime과 live A/B 스위치를 두지 않는다. cutover 전에 고정한 archived baseline과 새
단일 경로를 별도 실행으로 비교하고, 제품에는 새 경로만 남긴다.

★ **2026-09-04 축 재유도(사용자 결정).** B1·B2의 기준값이 잰 일과 새 경로가 재는 일이
달랐다 — B1 기준은 프로토타입의 **디코드**였고, B2 기준인 legacy `.asset` 읽기는
**텍스처도 신원 검증도 해시 검증도 하지 않았다**. 그래서 같은 일끼리만 비교 예산으로
판정하고, legacy에 대응물이 없는 칸은 archive 대비 비회귀로 본다. 임베디드 텍스처
디코드 칸은 **PHASE 12 T1a/T2로 이관**한다 — cook artifact를 디코드 완료 형식으로
바꾸면 사라지는 비용이고, 그 artifact 포맷은 그 페이즈의 소유다. 판정 규칙과 그것이
지키는 선은 [기준선 §4.4a](ModelAssetBigBangCutoverBaseline.md)에 있고, 게이트
`verify-model-cutover-budget`의 상수 블록이 같은 표를 들고 있다. 이관한 칸도 모델별
값으로 archive에 남아 회귀하면 붉어진다 — 예산에서 빼는 것이 아니라 판정 주체를 옮긴다.

- [x] Release cold load·warm load·reimport 시간 예산 충족 — **B1a 14/14·B2a 14/14 통과(2026-09-04, [기준선 §4.6](ModelAssetBigBangCutoverBaseline.md))**. 소스 디코드는 처음부터 14/14 예산 안이었고(Gunner 52.8 vs 158, scene 482 vs 859), CEMC 읽기·디코드·조립도 예산 안이다. 판정 전에 3.75가 소유한 낭비 둘을 걷었다 — 저작마다 asset root를 전수 재순회하며 sidecar를 재파싱하던 corpus collision 검사(파싱 캐시 — 평균 6.93 → 3.92 ms, 남은 것은 파싱이 아니라 순회다)와 게시 직전 source 전체를 다시 읽어 SHA-256을 다시 계산하던 race 검사(14모델 합계 302.6 → 48.1 ms, scene 212.9 → 2.8). B1b(저작 트랜잭션)·B2b(신원·해시 검증)·B2c(임베디드 텍스처 디코드 — PHASE 12)는 archive 비회귀 축이다. B3 씬 로드·B4 부팅도 통과. 전 corpus reimport는 14/14 합계 3.1 s(2026-09-03 실측).
- [x] model CPU resident·vertex/index·texture staging·peak VRAM 예산 충족 — B5 peak working set 832.8 MB ≤ 1,351 통과, peak VRAM 1,145 MB를 B6 archive에 기록(비회귀 ×1.10).
- [x] frame CPU record와 GPU pass timing 비회귀 — **기준선 기록 2026-09-03, 비회귀 판정 개시 2026-09-04**: corpus 재임포트(CEMC v4→v5, 14/14) 뒤 `dx12.scene`(FT_Primitives + Gunner)에서 메시 업로드 10개·1,075 KB가 전량 generation 경로, 커버리지 49,684, VRAM 1,145 MB를 `mbc11_perf_archive.json`에 기록했다. 이후 실행은 ×1.10 안이어야 한다. archive는 축 재유도와 함께 B1b·B2b·B2c의 **모델별** 시간(`authoringOverheadMs`·`identityVerifyMs`·`textureDecodeMs`)을 함께 들고, 시간 축은 흔들림 때문에 ×1.25(또는 +2 ms) 여유로 본다. 게이트는 **어느 칸이라도 붉은 실행**을 archive로 굳히지 않는다(회귀가 새 정답이 되는 것을 막는다).
- [x] cache hit/miss·generation retire 수가 읽기 전용 snapshot으로 관측됨.

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
   성능 검증은 §8.4 개정판을 뜻한다 — 비교 예산(B1a·B2a·B3·B4·B5)은 통과해야 하고,
   legacy에 대응물이 없는 칸(B1b·B2b·B6)은 archive 대비 비회귀로만 판정하며,
   임베디드 텍스처 디코드(B2c)는 **PHASE 12 T1a/T2**가 완료선을 소유한다.
   이관은 면제가 아니다 — 모델별 값이 archive에 남아 회귀하면 이 게이트가 붉어진다.

`MBC11` cutover가 승인된 뒤에는 legacy GUID 복원, runtime mapping, Assimp fallback, experiment
on/off를 복구 경로로 사용하지 않는다. rollback은 이전 release 전체로 되돌리는 배포 결정이며,
새 release 안에서 두 자산 체계를 다시 병행시키는 코드 경로가 아니다.

---

## 10. 변경 이력

| 날짜 | 변경 |
|---|---|
| 2026-09-04 | **MBC11 완료 — cutover. PHASE 3.75 종료.** 2026-09-03에 붉었던 B1·B2의 원인이 성능 회귀가 아니라 **축 불일치**였다는 것을 단계 분해가 보였고, 사용자 결정으로 §8.4의 축을 재유도했다(기준선 §4.4a). 비교 예산은 같은 일끼리만 판정하고(B1a 소스 디코드 · B2a CEMC 읽기/디코드/조립 — **각각 14/14 통과**), legacy에 대응물이 없는 칸은 archive 비회귀로 보고(B1b 저작 트랜잭션 · B2b 신원·epoch·SHA 검증 · B6 frame/VRAM), 임베디드 텍스처 디코드(B2c)는 **PHASE 12 T1a/T2로 이관**했다 — cook artifact가 디코드 완료 형식이 되면 사라지는 비용이고 그 포맷은 그 페이즈의 소유라 여기서 만들면 T1a가 다시 정의할 포맷이 하나 더 생긴다. 이관은 면제가 아니다: 모델별 값이 `mbc11_perf_archive.json`에 남아 회귀하면 게이트가 붉어진다. 판정 전에 3.75가 소유한 낭비를 걷었다 — ① corpus collision 검사의 sidecar 파싱을 `(크기, mtime)` 지문으로 캐시(순회와 epoch 폐포 재검증은 남긴다), ② 게시 직전 source 전량 재해시를 `(크기, mtime)` 대조 우선으로(다르면 옛 방식대로 재해시). `corpus-collision-scan` 평균 6.93 → 3.92 ms, `publish` 14모델 합계 302.6 → 48.1 ms(scene 212.9 → 2.8). ①의 착수 가정은 절반 틀렸다 — 남은 4 ms는 파싱이 아니라 asset root 전체 순회이고, 순회를 캐시하면 새 sidecar를 놓쳐 충돌 검사가 눈멀므로 남겼다. 로드 경로에서는 `cemc-read`와 `cemc-sha`를 갈라 "읽기는 비교 대상이고 해시는 아니다"를 수치로 보이게 했다. 후보 ④(SHA 가속)는 이 기준선 장비에서 답이 아니다 — **Xeon W-2223(Cascade Lake-W)에 SHA-NI가 없다**(기준선 §4.4a). 게이트는 어느 칸이라도 붉은 실행을 archive로 굳히지 않는다. 재측정 전량과 before/after는 [기준선 §4.6](ModelAssetBigBangCutoverBaseline.md). 모델·cook 게이트 11종(authoring-transaction·asset-identity·corpus-v8·corpus-v8-cutover·asset-generation·cutover-freeze·scene-consumption·typed-consumers·experiment-asset-cooker·experiment-cooked-catalog·experiment-model-cook-all) 전부 초록 — **Debug/Release AssetCooker와 Debug/Release Editor를 다시 빌드한 뒤**의 값이다. 처음 초록은 2026-09-03자 AssetCooker를 재고 있었다(모델 게이트 6종의 기준 바이너리가 `Bin/x64-Debug/Tools/AssetCooker`다). 그 재실행에서 `verify-experiment-asset-cooker`가 붉었는데 이 페이즈와 무관한 선행 결함이었다 — `GBuffer.shadermeta`의 `source:`가 `.hlsl`에서 `.slang`으로 옮겨간 커밋(`5ac55b17`) 뒤로 fixture가 옛 이름을 하드코딩해 복사하고 있었다. 이름을 문서의 `source:` 줄에서 유도하도록 고쳐 초록 복귀 |
| 2026-09-03 | **MBC11 검증 완료 — cutover 보류.** cook 경로를 하나로 만들었다: `ModelGenerationExportProducer`(namespace `experiment::cooked`)가 v2 sidecar → 게시된 generation을 런타임 리더로 검증한 뒤 generation 디렉터리 전체를 `Derived/Models/<xx>/<ModelId>/<gen>/`로 내보내고 kind Model(record, formatVersion 1)·Material·Texture(`textures/<id>.png`)·Mesh(record 경로) manifest 항목을 낸다. AssetCooker는 `--generation-root`(기본 `<asset-root>/../Library/ModelAssetGenerations`)·identity header를 받고, 폐포 sweep이 Model 레코드 디렉터리 안 파일을 폐포로 세며, 260자 경계는 `LongPath`로, WIC는 `CoInitializeEx`로 연다. manifest·source identity가 UUIDv8을 받는다(`IsCookedAssetId`). `ModelCookProducer`·v1 sidecar 리더 삭제. `DataSystem::LoadModelAssetGeneration`은 cooked catalog(`ResolveCookedArtifact` → generation.asset) → Library 순으로 해석하고 출처 계수(`generationFromCatalog/FromLibrary/LoadFailed`)를 `assets.modeldiag`에 낸다. `verify-experiment-cooked-catalog`(mounted 8/0·stale·unmounted 0/8)·`verify-experiment-asset-cooker`(UUIDv8·relocation) GREEN 복귀. 계측 공백을 닫았다: `assets.modelbench <dir> N [cooked|author]`(임시 프로젝트 사본 in-process 저작·재로드, peak working set·VRAM), `LoadModelAssetGeneration`/`AuthorModelAsset` 결과의 `phases` 단계 분해. `verify-model-cutover-budget`(Release 전용, run-all 배선)로 §8.4를 쟀다 — B3(Test1 15.2/FT 11.8 ms)·B4(31.1 ms)·B5(833 MB) 통과, B6 기준선 기록(메시 업로드 10개/1,075 KB·VRAM 1,145 MB), **B1 10/14·B2 12/14 초과**. 그 과정에서 corpus 14개를 재임포트했다(다른 세션의 CEMC `kFormatVersion` 4→5 상향으로 Library 전량이 거부되던 상태 → `AssetCooker --author-model-asset` 14/14 성공, **assetId 324개 전부 보존**·generation만 상승, 저장 참조 rewrite 불필요, 합계 3.1 s). 재임포트 뒤 `verify-model-asset-generation`(corpus 14/14·descriptor 356)·`verify-experiment-cooked-catalog`·`verify-experiment-asset-cooker`가 초록 복귀했고, cooker 게이트의 기대 파일 수 상수 4를 게시 generation에서 유도하도록 고쳐(변이로 이빨 증명: 텍스처 1개 삭제 → files=5/6·generationFiles=3/4 RED) 내보내기 폐포를 실제로 검사하게 했다. 원인·수치·후보는 [기준선 §4.5](ModelAssetBigBangCutoverBaseline.md). 미커밋 ~200파일(다른 세션의 MBC3~MBC6·CEMC v5 상향과 섞임), `ProjectSetting/AssetIdentity.asset` untracked, `verify-experiment-document-cook-parity`는 로컬 미추적 씬 6개로 MBC11과 무관하게 붉다 |
| 2026-09-03 | **MBC10 완료.** 제품 경로의 무조건 stdout 진단(`[mesh.resolve]`·`[model.instantiate]`·`[anim.tick]`)을 걷고 `Engine/SceneRuntime/ModelConsumptionDiagnostics`(원자 계수, 리셋 없음)로 대체했다 — 읽기 전용 `assets.modeldiag`가 `meshResolveGeneration/meshResolveFailed/instantiateGeneration/instantiateRejected/tickGeneration/tickNone/lastInstantiated`를 낸다. `experiment.animlive`는 더 이상 `Scene::PublishAnimatorPose`를 부르지 않고(상태 변경) 제품 barrier(AnimationJob)가 남긴 마지막 메트릭 스냅샷 `Scene::TryGetLastAnimatorPoseMetrics`를 읽는다(`source=product`). `verify-mbc-cutover-freeze`에 §5.2/§8.2 하드 계약을 더했다: 제거 표면 12종 하드 0·vcpkg assimp 0, `m_hashingMesh` 절차 지오메트리 허용목록 10파일 밖 0, 제품 소비자(SceneRuntime·Render·PrimitiveRenderProxy·EngineGUIWindow)의 `experiment::Model`/`TryGetMesh` 0, generation 게시 진입점 DataSystem 하나, animlive 읽기 전용·`assets.modeldiag` 존재. typed-consumers·scene-consumption·drop-animation·skin-pose-visual이 스냅샷 파싱으로 전환됐고 제품 stdout 토큰 재유입을 단정한다. §8.2 전항·§8.3 전항을 게이트 근거와 함께 체크했다(§8.4 성능 예산은 MBC11) |
| 2026-09-03 | **MBC9 완료.** legacy 모델 타입·로더(`Model`·`ModelLoader`·`SkeletonLoader`·`AnimationLoader`·`Skeleton`·`Animation`·`AnimatorData`·`ModelAssetFormat`)와 역브리지 TU(`ExperimentModelMigration.cpp` — `BuildLegacyModelFromExperiment`·`LoadModelViaExperiment`·`CREATOR_EXPERIMENT_VERTEX`·병행 바인딩 맵)·`ModelSceneBridge.cpp`·Assimp include 10건/vcpkg port를 물리 삭제했다. cooked catalog 함수는 `DataSystem.cpp`로 이주. RHI는 `GetOrUploadModel`(typed)과 절차 지오메트리용 `GetOrUpload(Mesh*)` 둘만 남고 `RHIExperimentVertexView`·`GetOrUploadExperiment`·lookup 주입·experiment 계수가 사라졌다(`EnhancedDrawItem::experimentView`·`experimentSource`·와이어프레임 배치 키 → `geometryKey`). 컴포넌트는 `MeshRenderer::m_Mesh`(리플렉션 골든 갱신)·`m_experimentModel`·`EnsureExperimentBinding`, `Animator::m_Skeleton`·`m_experimentModel`·`EnsureExperimentAnimationBinding`(→ `EnsureAnimationBinding`), `FoliageType::m_mesh`·`m_experimentModel`(→ 이름 → generation 바인딩), `AvatarMask::MakeBoneMask(Bone*)`가 은퇴했고 `AnimatorDataPath`는 `{None, Generation}`이다. 씬 인스턴스화는 `ModelSceneInstantiation::Instantiate(scene, generation, options)`(sidecar `ModelImporter.CreateMeshCollider` → `DataSystem::ReadModelCreateMeshCollider`)로, 에디터 드롭/undo 커맨드·`model.load/loadcached/place`·`App` 드롭·에셋 번들 창(`SnapshotCurrentModelAssetGenerations`)·리소스 카운터가 여기에 붙었다. `BoneRegion.h`가 `BoneRegion`·`ToLower`·`MAX_BONES`를 승계한다. 진단은 legacy/experiment 축을 걷고 typed 단일 축으로 재단(boneresolve/animmask/editorsurface/foliage/skinbounds — packed 정점을 `ModelVertexLayout` 표로 디코드/animevent/animlive/animtick), `experiment.model/modelbridge/anim/phase17model/import/gltf/fbx/embedded/bench/meshbounds`와 parity selftest 6쌍이 죽었다. dx12/vulkan `scene.glb` 16MiB 슬라이스와 기즈모 하네스는 `LoadModelAssetGenerationByPath`+`RHIModelMeshView`로, `scene.transformbulk` 프로브는 합성 Skeleton 대신 Gunner/SU generation rebind로 전환. 게이트: 동결 래칫 제거 표면 전부 0 재기준(`m_hashingMesh` 10은 비모델 절차 지오메트리 키), `verify-legacy-skeleton-retirement` 0-래칫(잔존 2 = 구 씬 YAML 키 문자열), `verify-model-typed-consumers`(스위치 없음·legacy 파일 부재·Assimp 0)·`verify-model-scene-consumption`(`unbound=0`·업로드 총계==generation)·`verify-editor-drop-animation` 갱신, `verify-experiment-vertex-live`·`verify-phase17-local-model-corpus` 은퇴 |
| 2026-09-03 | **MBC8 완료.** Animator가 `ModelAssetGeneration`을 붙들고 재생·본 해석·신원·클립·마스크가 typed 첫 축(Generation → Experiment → Legacy). `Assets/ModelAnimationSampler` typed 샘플러 + `AnimationJob` 틱 뷰 템플릿 단일화 — experiment 골든 `poseDigest=8042DC1C` 비트 동일, 스위치 off에서도 동일(재생의 스위치 의존 은퇴). Foliage `m_modelGeneration` typed 뷰·closure 텍스처, 인스펙터 typed read-only 블록, cook 씬 참조 스캔·재질 texture GUID 파서 v8 수용(MBC7 저장 씬이 `scene.reference`에서 거부되던 자리). Collider는 지오메트리 미소비(PHASE 19)·CLR은 이름 API뿐이라 이관 대상 없음을 실측으로 확정. 진단 6종에 `generation` 축, 신설 `verify-model-typed-consumers`, vertex-live·drop-animation typed 축 갱신 |
| 2026-09-03 | **MBC7 완료.** `MeshRenderer`가 `ModelAssetGeneration` handle과 영속 `m_meshAssetId`(UUIDv8 MeshId)를 붙들고 프록시→drawPool이 `RHIModelMeshView`를 채운다(MBC6이 비워 둔 자리). 씬 배치는 generation 직행이 정본, 재질은 generation material에서 시공, embedded texture owner는 `ResolveModelGenerationTexture` closure 캐시({TextureId, generation}, generation 단위 retire)에서 온다 — 등록부·이름 폴백·로드 순서 의존 제거(§6.2). 실측 결함 셋을 고쳤다: 이름 역해석이 v8 신원을 v4 PNG GUID로 덮던 저장 경로, 재질 코덕 reader의 v4 전용 파서(writer는 v8을 적고 있었다), v1 sidecar 리더의 v2 거부로 legacy 재질이 embedded GUID를 잃던 경로. 게이트 `verify-model-scene-consumption`: FT_Primitives+Gunner renderer 10/10 typed, Gunner 6/6 closure(등록부 0), 콜드 재로드 동일, reimport 재사용 0/retire 6, 실GPU 업로드 10/10 typed. vertex-live·drop-animation은 typed 축 수용으로 갱신, MBC3~MBC7 게이트 6종 run-all 편입, reflect 골든은 `m_meshAssetId` 한 줄만 달라 재생성 |
| 2026-09-02 | **MBC6 완료.** `ModelVertexLayout`을 Assets 정본으로 승격하고 core/color/skin/color+skin 네 전체 mask를 input layout·shader permutation·PSO key에 그대로 사용한다. `RHIModelMeshView`가 `{ModelId,MeshId,generation}`과 generation upload descriptor를 검증하며 DX12/Vulkan mesh cache와 GBuffer/Forward/Shadow가 이를 최우선 소비한다. Shadow는 pass-consumed attribute를 전체 layout과 분리하되 bone offset을 원본 mask에서 유도하고, Forward는 t12 palette와 instance bone offset으로 실제 skin pose를 계산한다. SU는 mask 247, stride 84, bone 64/68을 만족한다. `verify-model-render-wiring`이 CPU 4-mask·corpus 14/14와 DX12/Vulkan GBuffer typed upload 1/1, DX12 skinning, Vulkan Shadow/GBuffer/Forward 실실행, Vulkan validation 0을 Debug/Release에서 통과했다. Scene/MeshRenderer typed view 공급은 MBC7, legacy upload arm 삭제는 MBC9로 남는다 |
| 2026-09-02 | **MBC5 완료.** `ModelAssetGeneration` immutable aggregate와 `{ModelId,generation}` cache, `{ModelId,MeshId,generation}` handle, atomic publish/replace/retire 및 read-only snapshot을 추가하고 `DataSystem` schema v2 catalog와 load/reload/remove/finalize에 연결했다. canonical sidecar·generation record·CEMC·embedded texture SHA-256과 subasset closure를 게시 전에 검증한다. 실 corpus cold-load가 `Cha_Mon_5.fbx`의 animation-only derived skeleton inventory 누락을 잡아 writer에 신규 UUIDv8 skeleton 발급과 sidecar↔ModelDraft 수 검산을 추가했다. 현재 model 14/14, subasset 310, UUIDv8 identity 324, mesh 130/material 52/texture 96/skeleton 4/animation 28/descriptor 356이다. generation 1→2·변조 4종·stale/collision·retire gate를 Debug/Release에서 통과했고 identity 184, sidecar 85, MBC3/MBC4/freeze, Debug/Release AssetCooker·CreatorEditor도 통과했다. legacy adapter/소비자 제거는 MBC6~MBC9 범위로 남는다 |
| 2026-09-02 | **MBC4 완료.** `Invoke-ModelAssetV8Cutover` one-shot offline transaction과 `AssetCooker --issue-model-identity-epoch`을 추가했다. 실제 corpus에 `2026-09-model-bigbang` epoch를 발급하고 model 14/subasset 309/UUIDv8 identity 323/cooked generation 14를 게시했으며 `FT_Primitives.creator`의 legacy model 참조 8건을 새 ModelId로 rewrite했다. old model/subasset identity 68개는 migration 메모리에서만 사용하고 게시 mapping/alias는 만들지 않았다. 결과는 old 저장 참조 0, source 변조 0, UUID 충돌 0이다. 실패 주입은 generation·sidecar를 게시한 뒤에도 전체 원본 snapshot을 복구했고, 합성 material/texture 참조와 epoch overwrite 거부를 함께 검증했다. PHASE 17 Strict는 비모델 v4 212/model v8 14/subasset v8 309 혼합 계약으로 갱신했고 구 experiment 전수 cook 진입점은 v2 generation gate로 전환했다. Debug/Release AssetCooker·CreatorEditor 빌드와 identity/sidecar/MBC3/MBC4/freeze 게이트를 통과했다 |
| 2026-09-02 | **MBC3 완료.** `ModelAssetAuthoringTransaction`을 정식 Assets 계층에 추가해 프로젝트 단위 writer lock, source/import·stable-key·UUIDv8 corpus 검사, schema v2+CEMC+embedded texture+generation record staging/재판독, generation 우선 rename·canonical sidecar 최종 commit/rollback을 구현했다. Editor create/modify/move/import와 AssetCooker를 단일 API에 연결하고 `ModelIdentityRefresher` pseudo-v5-as-v4를 삭제했다. glTF `extras.creatorEngineId`를 mesh/material/image/skin/animation stable key 입력으로 전달한다. Prim_Cube 복제본 generation 1→2, UUIDv8 4개 유지, 실패 주입 5단계 delta 0, collision 거부, Debug/Release AssetCooker·CreatorEditor와 identity 184·sidecar 84 단정을 통과했다. 실제 corpus와 참조 rewrite는 MBC4로 남겼고, legacy 전수 cook 게이트의 `scene.glb.meta` v1 closure RED도 MBC4 진입 차단점으로 기록했다 |
| 2026-09-02 | 신설. 구 PHASE 4 I/V experiment 배선을 PHASE 3.75 단방향 cutover로 대체. 기존 GUID 비승계, UUIDv8+SHA-256 profile, schema v2 원자 writer, `ModelAssetGeneration`, SU/Gunner 폐쇄 조건, legacy/A-B/Assimp 제거, 60일 공수와 게이트를 확정 |
| 2026-09-02 | **MBC0 완료.** 동결 래칫 게이트(표면 15·하드 계약 3), corpus 14/참조 28 분류(모델 참조 8·subasset 참조 **0**·고아 11), Prim sidecar 8개 손상의 원인 경로(워처 Delete 오독 → `CreateMetaLocked` 재발급) 실측, Release 기준선·예산 B1~B6 고정. 미커밋 폴백 덧대기(MeshRenderer 순서 해킹·`[material.finalize]`)와 손상 sidecar는 stash로 걷어냈다. **계측 공백**: 새 경로 cooked 읽기·frame CPU/GPU·peak VRAM은 CLI가 없어 MBC11 전에 세워야 한다 |
| 2026-09-02 | **MBC2 완료.** epoch header(`ProjectSetting/AssetIdentity.asset`, CSPRNG 256-bit), stable key 문법·규칙 엔진(semantic/authoring, 지문 재결합, 모호성=고아 prior+새 지문 동시), sidecar v2 코덱(왕복·다른 키 보존·legacy guid 제거·v1/ordinal 거부)·폐포 검증(재유도·registry bijection). 실자산 14 모델 임포트→배정→v2→폐포 통과, 전 corpus registry 309 충돌 0, 같은 입력 재배정 동일 신원. 첫 규칙은 scene.glb의 동일 지오메트리 무명 메시에서 변경 없는 재임포트를 거부했다 — 지문 그룹 안 binding 순 짝짓기로 정련. 디스크 쓰기 없음(원본 해시 전후 동일 게이트). §3.1에 확정 사항 기록 |
| 2026-09-02 | **MBC1 완료.** §2 바이트 계약을 `assets::DeriveIdentity`로 구현(헤더 온리 SHA-256, UTF-8 well-formed·NFC fail-closed, 길이 접두 U32BE, v8/variant bit), 계층 `DeriveModelId`/`DeriveSubAssetId`(legacy v4 namespace는 값에서 거부), `IdentityRegistry`(DuplicateTuple/UuidCollision/RecomputeMismatch, canonical tuple = 입력 바이트열). test vector 15건은 Python 독립 유도로 생성하고 .NET이 3차 검산. **변이 검증**: 길이 접두를 LE로 바꾼 제품 빌드에서 게이트 RED(단정 86 실패), 되돌리면 GREEN. pseudo-v5-as-v4 접촉 2+1은 MBC3에서 0으로 내려 래칫을 갱신했다 |
| 2026-09-02 | **PHASE 17 동기화 충돌 해소.** 코드 충돌 2곳(Console 명령 등록, SerializationPlan)을 병합하고, yaml-cpp 은퇴와 충돌한 MBC2 epoch/sidecar 코덱을 ryml 저작 문서 경계로 이식했다. PHASE 17 D2의 UUIDv4 방향은 역사·MBC4 입력 기준선으로 강등하고 최종 identity authority를 이 문서의 UUIDv8로 단일화했다. 현재 디스크 sidecar 226개는 여전히 UUIDv4이므로 실제 corpus cutover는 MBC3~MBC4 미완료로 유지 |
