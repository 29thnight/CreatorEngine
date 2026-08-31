# 모델 임포트 파이프라인 · 정점 레이아웃 (PHASE 4)

2026-08-25 신설(당시 PHASE 24), 2026-08-25 **PHASE 4 로 편입**,
2026-08-27 생산 배선·머테리얼 교차 의존 재감사 및 V2/V3 구현. glTF·FBX 임포터와 그
사이 계층이 이미 서 있고 게이트도 도는데, **계획 문서만 없었다.** 지시 단위로
진행돼서 단계 정의·완료 기준·순서 제약이 어디에도 적혀 있지 않다.

목표 사슬:

```
파일 → IAssetImporter → ImportedScene(IR) → [후처리] → ModelDraft → ModelLoader → Model
        gltf / fbx                        탄젠트·용접·LOD    ↑ 손실은 여기 한 곳
                                                             ↓
                                                    정점 레이아웃 → GPU (트랙 V)
```

---

## 0. PHASE 4 편입 — 왜 별도 페이즈가 아닌가

이 계획은 독립 페이즈(PHASE 24)로 신설됐다가 PHASE 4(차세대 렌더러 기능)로
편입됐다. 근거는 **PHASE 4 의 네 기능이 전부 이 파이프라인의 출력을 입력으로
요구한다**는 것이다.

| PHASE 4 기능 | 이 계획에 거는 것 |
|---|---|
| GPU-driven rendering | 메시가 공통 버퍼에 들어가려면 정점 레이아웃이 **PSO 분류의 축**이 된다. meshlet 빌드(`buildMeshlets`)의 소비자가 바로 이것이다 |
| Stochastic Tile-Based Lighting | 정적/동적 메시 구분이 히스토리 유효성의 입력 |
| DXR | BLAS 빌드가 정점 버퍼의 **포맷과 stride 를 직접 받는다**. 96B stride 로 BLAS 를 만들면 그 낭비가 가속 구조 빌드 시간·메모리에 그대로 간다 |
| DLSS | 모션 벡터가 이전 프레임 정점 변환을 요구 — 스킨/정적 구분이 그 경로를 가른다 |

그리고 `ScriptableRenderPipelinePlan`(같은 PHASE 4 의 첫 계약)이 **Pass 를 Asset
으로 기술**하기로 확정했는데, Pass 가 정점 레이아웃 오프셋을 C++ 에 손으로 박고
있으면 그 계약이 성립하지 않는다. 현재 오프셋은 **5곳에 손으로 반복**돼 있다(§1.7).

경계는 이렇게 나눈다.

- **이 계획(트랙 V)** — 정점 데이터가 *어떤 모양으로* GPU 까지 가는가.
  속성 기술표·메시별 마스크·스트림 분리. **대상은 experiment 계층뿐이다**(§4-V 서문).
- **`ScriptableRenderPipelinePlan`** — Pass 가 그 모양을 *어떻게 소비*하는가.
  Pass 가 "요구 속성"만 선언하고 입력 레이아웃을 유도받는 계약.

`buildMeshlets` 가 "mesh shader 소비자가 생길 때까지 보류"라고 적혀 있었는데
(§1.3), **그 소비자가 PHASE 4 의 GPU-driven 이다.** 편입으로 그 보류 사유가
해소된다.

---

## 1. 지금 무엇이 있는가 — 측정 (2026-08-27 재감사)

### 1.1 ★ 생산 소비자가 0이다

`experiment::` 를 참조하는 파일을 전수 검색한 결과, **전부 검사 하네스다.**

| 참조하는 곳 | 성격 |
|---|---|
| `Editor/RenderTests/ExperimentParity/` 7쌍 | 자가 검증 |
| 그 외 | **없음** |

화면에 나오는 것은 여전히 legacy(`Model::LoadModelShared` + Assimp)다. 지금까지
만든 것은 콘솔 커맨드로만 도는 **병렬 경로**다.

이건 착수 단계에서는 옳은 모양이지만, 길어지면 "만들어 놓고 아무도 안 쓰는
파이프라인"이 된다. 이 저장소에는 이미 그렇게 죽은 것들의 기록이 있다
(RhiBoundaryPlan §1.1 "소비자 없는 추상"). **이 페이즈의 존재 이유는 그 상태를
끝내는 것이다.**

### 1.2 소스·cooked 디코더는 섰지만 선택 정책은 아직 없다

I1 이후 `ImporterModelDecoder`가 확장자로 glTF/FBX 임포터를 골라
`Import → ConvertToModelDraft → ModelDraft`를 실제로 수행하고, 대표 glTF·FBX·animation
게이트가 이 디코더를 탄다. I7의 `CookedModelDecoder`도 별도로 존재한다.

남은 단절은 **두 디코더 사이의 정책**이다. `ModelLoader`는 생성 때
`unique_ptr<IModelDecoder>` 하나만 받으므로 `CookedThenSource`를 보고 cooked 거부 뒤
source로 폴백할 수 없다. `SourceOnly`와 `CookedOnly`는 각각 전용 디코더를 직접 꽂아
검사할 수 있지만, 셋을 고르는 제품 resolver와 `DataSystem` 소비자는 없다. 따라서
I1의 내부 소스 사슬은 완료됐어도 §1.1의 "생산 소비자 0" 판정은 그대로다.

### 1.3 선언만 있고 소비자 0인 옵션

| `ImportOptions` 플래그 | 소비자 |
|---|---|
| `generateMissingNormals` | 1 (ufbx 전달) |
| `generateMissingTangents` | 1 (mikktspace 패스) |
| `weldVertices` | **0** |
| `optimizeVertexCache` | **0** |
| `lodLevels` | **0** |
| `buildMeshlets` | **0** (mesh shader 소비자 생길 때까지 의도적 보류) |

### 1.4 측정된 잔여 손실

변환 경계가 계수하고 있는 것들이다. 전부 로그에 숫자로 나온다.

| 항목 | 실측 | 성격 |
|---|---|---|
| 임베디드 텍스처 | Gunner 재질당 ×3 property 생략 | `resolveTextureAsset` 정책 부재 |
| `doubleSided` · `emissiveStrength` | 재질마다 1건 | ModelDraft 에 자리 없음 |
| influence 5개 초과 | FBX 메시 3개에서 255·45·66건 | 설계상 클램프(상한 4) |
| FBX 계단 키 | Linear 키 쌍으로 펴짐 | 값 보존, 표현만 다름 |
| 탄젠트 이음매 분리 | Gunner +383, FBX +11 | 정상 동작(mikktspace 규약) |

### 1.5 검사 커버리지의 약점

- **FBX 자산 2개**뿐이고 그중 `Cha_Mon_5.fbx` 는 Assimp 가 못 읽어 **비교
  기준선이 없다.** 자체 검증만 통과한 상태다.
- **실자산에 값이 변하는 Step 트랙이 없다**(glTF 455개 전부 상수). 합성 검사로
  메웠지만 실자산 경로는 미검증이다.
- **실자산 게이트가 탄젠트 재용접 결함을 못 잡는다.** 변이 실험으로 확인했다 —
  용접 키에서 탄젠트를 빼도 영벡터 0·비단위 0·비직교 0 으로 전부 깨끗하다.
  합성 검사(`experiment.tangent`)만 판별했다.
- `COLOR_1`은 아직 단일 런타임 컬러 슬롯 범위 밖이다. `SU_Mythic.glb`가
  `COLOR_0`·`COLOR_1` 두 세트를 가진 유일한 자산이며 V3는 `COLOR_0`을 보존하고
  `COLOR_1`은 명시적 경고를 남긴다. 이 자산은 2026-08-25에 AABB 편차 0.055344로
  실패했지만 2026-08-27 현재 게이트에서 **AABB 편차 0 · 구조 불일치 0 · 오류 0**으로
  통과한다. 과거 실패를 현재 차단으로 세지 않는다.

### 1.6 서 있는 것 (완료된 자산)

| 계층 | 파일 | 상태 |
|---|---|---|
| 데이터 모델 | `Experiment/ModelData.h`, `Model.*`, `ModelLoader.*` | 검증·게시 완결 |
| 임포트 IR | `Import/ImportedScene.*` | 계약 검증기 포함 |
| 생산자 | `Import/GltfImporter.*`, `Import/FbxImporter.*` | 2종 |
| 후처리 | `Import/TangentGeneration.*` | mikktspace |
| 변환 경계 | `Import/SceneToModelDraft.*` | 손실 단일점 |
| 벤더링 | `ThirdParty/{fastgltf, ufbx, mikktspace}` | 원본 무수정 |
| cooked | `Experiment/Cooked/CookedModelFormat.h`, `CookedModelCodec.*` | I7 포맷·코덱 완료 |
| 게이트 | `experiment.{model,anim,bench,import,gltf,fbx,sampler,tangent,cooked,normal,weld,cacheopt}` | 12종 |

### 1.7 정점 레이아웃 — 실측 (2026-08-25)

캐시 자산 14개를 직접 파싱해 **정점 233,910개 전수**를 쟀다. 스크립트는
scratchpad 에 있고 파싱 정합은 tail 바이트로 확인했다.

**legacy 96B 가 GPU 로 그대로 올라간다.** `DX12MeshCache.cpp:214`/`:287` 이
`vertices.data()` 를 `sizeof(Vertex)` stride 로 업로드한다. 중간 변환이 없으므로
**레이아웃 절감 = 대역폭 절감**이다. Vulkan 도 같다(`VulkanRenderServices.cpp:703`).
아래 표는 현재 화면을 그리는 legacy 생산 경로의 기준선이다. `experiment::Vertex`는
V2에서 68B로 줄었지만 아직 생산 렌더 소비자가 없어 GPU에는 올라가지 않는다(§1.8).

| 필드 | off | 크기 | 실측된 상태 |
|---|---|---|---|
| position | 0 | 12 | 정상 |
| normal | 12 | 12 | 정상 |
| uv0 | 24 | 8 | 정상 |
| **uv1** | 32 | **8** | **입력 레이아웃 0곳 · CPU 소비자 0 · 전수에서 `uv1 != uv0` 0건** |
| tangent | 40 | 12 | 정상 |
| **bitangent** | 52 | **12** | **부호 1비트만 나른다** |
| **boneIndices** | 64 | **16** | **정수를 float 로 — 실측 최댓값 60** |
| boneWeights | 80 | 16 | 정상(합=1 위반 0건) |

- **uv1** — 원본 자산 12개 전부 `TEXCOORD_1` 이 없어 `Mesh.h:87` 이 uv0 를
  복사한다. 라이트맵 UV 로도 쓸 수 없다 — uv0 는 타일링이라 `scene.asset` 범위가
  `[-27.79, 32.29]` 이고 정점의 7.2% 가 `[-2,2]` 밖이다.
- **bitangent** — `GBuffer.hlsl:148`·`ForwardShade.hlsl:323` 이 `b = cross(n,t) *
  handedness` 로 재계산한다. `|bitangent|` 는 전수 1.0. 셰이더 계산을 그대로
  재현해 CPU 사전계산 부호와 대조 → **전수 불일치 0**(경계 16건은 퇴화 정점이고
  양쪽 다 +1).
- **boneIndices** — 셰이더가 `(uint)input.boneIndices[i]` 로 캐스트한다.

**더 큰 낭비는 스킨이다.** 메시 52개 중 스킨 9 · 정적 43, 정점의 **89.6%**가
스킨을 쓰지 않으면서 32B 를 낸다.

| 낭비 | 총량 |
|---|---|
| 정적 메시가 쓰지 않는 skin 32B | **6.39 MB** |
| 전 정점이 쓰지 않는 uv1 8B | 1.78 MB |

정점 바이트는 캐시 파일의 **79.3%**를 차지한다(`scene.asset` 은 85.4%).

| 레이아웃 | 정점 총량 | 절감 | 손실 |
|---|---|---|---|
| 현재 96B 고정 | 21.42 MB | — | — |
| V2 무손실 68B 고정 | 15.17 MB | 29.2% | 없음 |
| **스트림 분리** | **11.17 MB** | **47.8%** | 없음 |

**양자화는 기각한다.** uv0 를 half2 로 줄이면 `scene.asset` 에서 왕복 오차가
최대 `0.01146` — 2048 텍스처 기준 23텍셀이다. float32 로 둔다.

**정본이 없다.** 같은 사실이 11곳에 손으로 반복된다.

| 지점 | 빠뜨리면 |
|---|---|
| 후처리 재용접(`NormalGeneration`·`TangentGeneration`) | **조용히 소실** |
| 변환 경계(`SceneToModelDraft`) | **조용히 소실** |
| 캐시 직렬화(legacy `ModelLoader` write/read) | 버전 필드가 없어 기존 캐시 오독 — **legacy 한정이고 트랙 V 는 건드리지 않는다**(§4-V 서문) |
| 입력 레이아웃 5곳 + `static_assert` | 셰이더가 0을 읽음 |
| 셰이더 `VSIn` 4곳 | PSO 실패 또는 0 |

V3 착수 전에는 그 결함이 실제로 있었다 — `streams.colors` 는 FBX 임포터가
채우지만 `SceneToModelDraft`가 읽지 않았고, glTF 임포터는 `COLOR_0`도 읽지 않았다.
2026-08-27 V3에서 둘을 닫았다. `COLOR_0` VEC3/VEC4(정규화 정수 포함)는
`streams.colors` → packed `VertexBuffer`로 보존하고, 단일 런타임 컬러 슬롯 범위 밖인
`COLOR_1`은 조용히 버리지 않고 `UnsupportedFeature` 경고를 남긴다.

### 1.8 ★ V2 68B + V3 메시별 packed buffer 완료, 생산 GPU 배선은 아직 없음 (2026-08-27)

V2 전에는 스킨 32B가 `std::array<BoneInfluence, 4>`의 `(bone, weight)` 인터리브라
legacy 입력 레이아웃의 분리된 `BLENDINDICES`/`BLENDWEIGHT`로 기술할 수 없었다.
소비자가 0이라 이 결함이 화면에서 드러난 적도 없었다.

V2가 그 구조를 아래처럼 바꾸고 속성 기술표와 직접 대조 단정했다.

| 필드 | 형식 | offset · 크기 |
|---|---|---|
| position · normal · uv0 | `float3 · float3 · float2` | 0 · 12 · 24 / 32B |
| tangent | `float4` (`w` = handedness) | 32 / 16B |
| boneIndices | `uint8×4` (`255` = invalid, 유효 최댓값 254) | 48 / 4B |
| boneWeights | `float4` | 52 / 16B |

합계는 **68B**다. 초안의 64B는 `48 + 4 + 16` 산술 오류였다. 64B로 더 줄이려면
네 번째 weight 재구성 같은 별도 압축 계약이 필요하므로 이번 무손실 슬라이스에
몰래 넣지 않았다. `BoneInfluence`는 임포트 변환용 scratch 표현으로만 남고 런타임
정점에는 인터리브되지 않는다.

V3는 `Mesh::vertices`를 논리 `Vertex` 벡터가 아니라 mesh별 mask/stride를 가진 packed
`VertexBuffer`로 바꿨다. 코어만 가진 정적 메시는 실제 48B, 스킨 메시는 68B이며
source-authored uv1/color는 존재할 때만 8B/16B가 붙는다. `VertexLayout.h`가
offset·stride·레이아웃 표 해시의 정본이고 cooked v3도 헤더의 table hash/mask union/
max stride와 mesh별 mask/stride를 교차 검증한다. 거부된 cooked 입력은 부분적으로
채워진 draft를 노출하지 않고, 완전히 디코드된 임시 draft만 원자적으로 게시한다.

검증: Debug x64 `RenderTests`·`CreatorEditor` 빌드 성공. 12개 experiment 게이트
(`model`·`anim`·`bench`·`import`·`gltf`·`fbx`·`sampler`·`tangent`·`cooked`·
`normal`·`weld`·`cacheopt`)가 통과했다. 실자산 벤치는 legacy 96B 대비 experiment
68B, 정점 바이트 **0.86MB → 0.63MB**를 보고했다. V3 최종 검증에서 정적
`Prim_Suzanne`은 **0.10MB → 0.05MB**(실제 48B), 컬러 포함 `SU_Mythic`은
`color mesh 1 · layout 위반 0`, cooked 왕복 **6,217/6,217**을 확인했다. 프로젝트
전체 **11.17MB/47.8%**는 233,910정점 감사에서 계산한 전망이며 이번 실행에서 전 자산을
다시 합산한 값은 아니다. 생산 렌더 소비자가 없으므로 이 증거도 구조·직렬화·CPU
파리티까지이고 픽셀 파리티는 아니다.

### 1.9 라이트맵 — uv1 의 소비자가 삭제됐다

`Interfaces/LightMapping.h` 가 `lightmapIndex`/`Scale`/`Offset`/`Tiling` 을 든다 —
Unity 의 `lightmapScaleOffset` 과 같은 구조이고, 그 값은 **정의상 UV1 에 곱한다.**

| 단계 | 상태 |
|---|---|
| 에디터 저작 | 살아 있음 |
| 프록시 전파 | 살아 있음 |
| 셰이더 소비 | **0건**(hlsl 52개 전수) |
| 베이커 | 현 트리에 **없음** — 그러나 **삭제된 구현이 있다** |
| `lightmapIndex` 를 -1 아닌 값으로 쓰는 경로 | **0건** |

★ BVH 레이트레이싱 컴퓨트 베이커(C++ 1,538줄 + 셰이더 964줄)가 `ccca6964`(DX11 구
렌더러 철거)와 `24e784ce`(옛 셰이더 폐기)에서 삭제됐다. **그것이 `uv1` 의 원래
소비자였다:**

```cpp
t.lightmapUV0 = (vertices[i0].uv1 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
```

즉 §1.7 의 "uv1 소비자 0" 은 처음부터 없었던 것이 아니라 **렌더러 철거 때 함께
사라진 것**이다. 상세 분석과 재작성 계획은 `LightmapBakerPlan.md`(같은 PHASE 4 ·
트랙 L)에 있다.

**이것이 트랙 V 의 입력을 바꾼다.** 라이트맵은 폐기 대상이 아니다 — 현재 GI 가 전부
스크린스페이스(SSAO/SSGI/SSR/SSS)라 화면 밖 간접광을 아는 경로가 하나도 없고,
라이트맵이 그 공백을 메우는 가장 싼 길이다(`LightmapBakerPlan` §0). 따라서 **UV1 은
트랙 V3 옵셔널 스트림의 확정된 첫 소비자**다.

단, **지금의 uv1 슬롯은 그 용도로 쓰이지 않는다.** 라이트맵 언랩은 이음매에서 정점을
쪼개 정점 수 자체를 바꾸므로, 그 단계에서 스트림을 새로 만든다. 원본 자산 12개에
`TEXCOORD_1` 이 0건이므로 어차피 언랩부터 해야 한다.

### 1.10 모델 ↔ 머테리얼 배선 재감사 (2026-08-27)

`experiment::Material`은 `assetId`·`shaderAssetId`·variant property·keyword selection을
이미 소유해 M5 목표 모양과 가깝다. 그러나 실제 변환 경계에는 세 가지 틈이 있었다.

- `SceneToModelDraft`의 기본 이름은 `_BaseColorFactor`·`_MetallicRoughnessMap` 계열인데,
  M5의 저장·복원 정본은 `baseColor`·`baseColorMap`·`ormMap` 계열이다.
- 변환기는 모든 재질에 `shaderAssetId` 하나를 복사하지만 `material.assetId`는 채우지
  않는다. validator도 두 nil ID를 거부하지 않아 렌더 불가능한 재질이 초록으로 게시될
  수 있다.
- M6-P1a가 `EnhancedDrawItem`의 immutable property snapshot을 연결한 뒤 P1b1은 texture owner,
  P1b2a는 texture GUID/register, P1b2b1은 keyword permutation, P1b2b2는 frame packet의 복수
  ShaderMeta generation과 material별 PSO까지 닫았다. M6-P2a는 Forward의 immutable
  value/texture-owner packet을, P2b는 별도 frame ShaderMeta owner·reflection b2/t4..t7·
  material별 일반/Reference PSO pair와 인접-only transparent batch를 닫았다. P2c는 실제
  Water/Wind Material의 `m_shaderMetaGuid`, Standard 48B prefix+custom float 4개의 64B `b2`,
  다음-frame property 변경을 양 backend에서 닫았다. 당시 canonical seed는 Scene/proxy 소유
  non-cache 대표 Material을 위한 고정 bridge였고 P2d-d의 Host required-asset packet으로 제거됐다.
  P2d-a는 `FoliageRenderProxy`의 type별
  mesh/material owner·instance world matrix·view별 AABB culling 입력을 실제 제품 draw pool에
  연결했다. P2d-b는 `m_flowInfo` wind/UV와 frame total/delta를 immutable flow snapshot·Forward
  instance로 연결해 양 backend 픽셀을 닫았다. P2d-c는 Material/DataSystem/draw snapshot의 texture
  owner를 임의 property 이름 vector로 일반화하고 `windMap@t4`를 양 backend에서 닫았다. P2d-d는
  활성 Scene의 Mesh/Foliage Material owner에서 pass별 GUID required packet을 만들고 고정 Water/Wind
  seed를 제거했다. P2d-e는 material-cache frame scan·raw texture alias/setter·제품 draw pool의
  legacy writer를 은퇴해 M6 전체를 닫았다. 이 legacy Material GUID 선택은 `experiment::Material`의 실제 asset/shader ID
  보존 증거가 아니다.

첫 틈은 `StandardMaterialProperty.h`를 이름 정본으로 세워 2026-08-27 닫았다.
`DataSystem`의 legacy texture GUID 이행과 `SceneToModelDraft` 기본 매핑이 같은 상수를
쓰며, glTF/FBX 실물 디코더 게이트가 게시된 재질에 표준 숫자 property 6개가 있고
폐기된 underscore 이름이 없음을 검사한다. **이 완료는 M6 draw 배선이나 재질 GUID
정책 완료가 아니다.** Debug x64 `RenderEngine`·`RenderTests`·`CreatorEditor` 빌드와
Gunner glTF(재질 2)·Ani_Mon FBX(재질 6) 실행에서 계약 위반 0건, 기존 구조/보간/
탄젠트 실패 0건을 확인했다.

---

## 2. 무엇을 하려는가

1. **legacy(Assimp) 를 이 경로로 대체한다.** 소비자 0 상태를 끝내는 것이 이
   페이즈의 본체다.
2. **남은 손실점을 닫는다.** 텍스처 해석·노멀 생성·meshopt.
3. **"손실은 한 경계에서만, 반드시 계수된다" 성질을 유지한다.** 이것이
   `ImportedScene` 을 따로 둔 이유이고, 전환 과정에서 깨지면 안 된다.
4. **정점 레이아웃의 정본을 세운다(트랙 V).** 지금은 정본이 없어 같은 사실이
   11곳에 손으로 반복되고, 그중 셋은 빠뜨리면 조용히 틀린다(§1.7). PHASE 4 의
   GPU 기능들이 이 레이아웃을 입력으로 받으므로 **그 전에 서 있어야 한다.**

---

## 3. 설계 — 이미 정해진 것

### 3.1 IR 은 의도적으로 부유하다

`ImportedScene` 은 `ModelDraft` 보다 많이 담는다. source 가 들고 온 것을 런타임
모델이 버리더라도 IR 에서는 일단 보존하고, 무엇이 죽는지는 **변환 경계 한 곳**에서
`ImportNote` 로 계수한다. 손실이 임포터 구현 안에 숨는 것을 구조적으로 막는다.

### 3.2 좌표·단위는 임포터가 통일한다

IR 이후의 어떤 코드도 포맷별 관례를 알 필요가 없다. 원본 값은 metadata 에 기록만
한다. **단위 환산은 하지 않는다** — legacy 가 하지 않으므로 여기서 하면 기준선과
어긋난다(FBX 는 보통 cm).

### 3.3 게이트 설계 — 실측으로 얻은 규칙 넷

이 페이즈에서 게이트가 거짓 초록을 낸 사례가 **네 번** 있었다. 규칙은 그 대가다.

1. **"보존됐나" 와 "실제로 그렇게 계산되나" 를 갈라 재라.** Step 트랙 455개
   보존·위반 0 이었지만 계단 계산은 한 번도 실행되지 않았다.
2. **0건일 때 왜 0인지 내역을 찍어라.** 빈 집합인지 손실이 없는 건지 구분이
   안 된다.
3. **불일치는 실값과 함께 보고하라.** 편차 스칼라 하나로는 축 부호 반전·단위
   어긋남·변환 누락이 전부 같은 숫자를 낸다. FBX AABB 1.501 의 원인을 가른 것이
   실값이었다(내 첫 가설은 틀렸다).
4. **새 검사가 첫 실행부터 초록이면 변이로 이빨을 증명하라.** 꼬리 보정 한 줄을
   지워 35건 중 6건이 정확히 그 결함만 짚는 것을 확인했다.

### 3.4 실자산 비교로 담보되지 않는 것이 있다

변이 실험이 증명했다 — mikktspace 헤더가 금지한 "기존 인덱스 재사용" 결함이
들어가도 실자산 지표는 전부 깨끗하다. **해석적 정답이 있는 합성 검사**가 따로
있어야 한다.

---

## 4. 이행 — 슬라이스

### I0. 계획·기준선 — **완료**

이 문서 + 게이트 8종 + 벤더링 3종. 실측 기준선이 §1 에 있다.

### I1. 실물 디코더 배선 — `ImporterModelDecoder`

임포터를 `ModelLoader` 에 꽂는 접착제. 지금은 검사가 손으로 잇고 생산 경로에는
없다(§1.2).

- 확장자로 임포터를 고르고 → `Import` → `ConvertToModelDraft` → draft 반환
- `ImportNote` 를 `ModelLoadIssue` 로 옮기는 규칙 확정(심각도 대응)
- 하네스가 `LegacyBridgeDecoder` 대신 이것을 쓰도록 전환 — **검사가 실물을
  타야 검사에 값어치가 있다**

의존: 없음. **가장 먼저 할 것.**

### I2. 텍스처 자산 해석 정책

`resolveTextureAsset` 이 비어 있어 임베디드 텍스처 property 가 생략된다(§1.4).
`.meta`/AssetId 발급 주체를 정해야 하므로 SerializationPlan(PHASE 17) 과 맞물린다.

- 임베디드 바이트를 디스크로 뽑는 시점·위치
- AssetId 발급 — 새로 만들 것인가 기존 자산에 붙일 것인가
- 뽑지 않기로 한 경우의 표현(지금은 property 생략 + 계수)

### I3. 노멀 생성 패스

`generateMissingNormals` 는 ufbx 에만 전달되고 **glTF 경로에는 생성기가 없다.**
법선이 없으면 탄젠트도 못 만든다(mikktspace 전제).

### I4. meshoptimizer 연동

죽은 플래그 넷(§1.3)을 살린다. `weldVertices` → `optimizeVertexCache` →
`lodLevels` 순. meshlet 은 mesh shader 소비자가 생길 때까지 보류.

★ **탄젠트 생성과의 순서가 중요하다.** 용접이 탄젠트 이음매를 다시 붙이면
mikktspace 규약이 깨진다 — 탄젠트를 정점 정체성의 일부로 다뤄야 한다.

### I5-0. Material/ShaderMeta 교차 계약 — **이름·쿠킹 게시 계약 완료, 제품 ID·소비 배선 잔여**

I5에서 `experiment::Model`을 직접 소비하기 전에 재질이 M5/M6 계약으로 손실 없이
건너갈 수 있어야 한다. 2026-08-27 첫 안전 슬라이스로 표준 PBR property 이름을
`StandardMaterialProperty.h` 하나로 모으고, legacy `DataSystem`과
`SceneToModelDraft`가 함께 소비하게 했다. glTF/FBX 게이트에는 게시 property 검사를
추가했다.

2026-08-29 현재 머테리얼 레인은 M5-C4까지 완료돼 M5를 닫았고, M6-P0의 격리된
Standard Material 숫자 `b2` 프로브, M6-P1a의 제품 GBuffer 숫자 property batch,
M6-P1b1의 Material/draw packet texture generation owner, M6-P1b2a의 texture property
GUID→ShaderMeta reflection register binding, M6-P1b2b1의 material keyword permutation PSO,
M6-P1b2b2의 multi ShaderMeta generation PSO도 양 backend에서 관통했다. 양 backend의
deep-owned graphics request, targeted invalidation/completion retirement와 대표 GBuffer
generation 전환에 이어 `FoliageType`과 Editor Undo의 장기 Model/Mesh/Material raw 소비를
소유 참조로 바꿨다. Model/Material은 전역 retired generation 없이 실제 consumer 수명만
따르며, Terrain/UI/Sprite 등 raw alias가 남은 texture 계열 보존은 제한적으로 유지한다. P1a는 `BuildDrawPool`의
최종 item에서 `Material*`를 제거하고 generation/layout/keyword/property/texture snapshot을
밀봉해 같은 mesh/texture의 다른 property를 GBuffer 2 batch로 나눴다. P1b1은 Material의
texture owner 5개와 GBuffer packet owner 4개를 연결해 Material 해제 뒤 packet 수명과 packet
해제 뒤 반환까지 닫았다. P1b2a는 제품 meta의 숫자 7개+texture 4개와 `t0..t3/space0`를
reflection으로 고정하고 packet에 property/GUID/register/owner를 함께 밀봉했다. P1b2b1은
같은 active meta 안의 keyword PSO를 닫았고 P1b2b2는 GT frame packet의 복수 meta owner와
  material별 meta+permutation PSO·targeted retirement를 닫았다. P2a는 Forward value/texture owner
  packet을, P2b는 제품 `Forward.shadermeta` generation, Standard `b2/48B`, `t4..t7`, material
  permutation별 일반/Reference PSO pair와 A/B/A 인접-only 순서 게이트를 닫았다. P2c는 실제
  `ForwardWater`·`ForwardWind` Material GUID가 별도 Meta와 64B custom numeric block을 고르고,
  7 draw/4 batch/3 meta, 양 backend overlap `0.125/0.25/0.5`, wind G `0→0.325`, backend 편차
  `0`과 다음-frame 변경을 닫았다. 그러나 이는 일반 transparent 경로의 대표 bridge다.
  P2d-a는 `FoliageRenderProxy`를 owning type/instance draw source로 제품 `BuildDrawPool`에
  연결하고 한 카메라의 `m_isCulled` 대신 view별 transformed AABB culling을 쓰게 했다.
  P2d-b는 dynamic time/flow·`m_flowInfo`를 immutable draw/instance 입력으로 옮기고 DX12/Vulkan
  동일 픽셀과 invalid time fail-closed를 닫았다. P2d-c는 generic texture schema/owner vector와
  `windMap@t4` owner 수명·픽셀을 닫았다. P2d-d는 cache 밖 Scene Material까지 pass별 required GUID
  packet으로 밀봉하고 canonical Water/Wind seed를 제거했다. P2d-e는 전체 legacy 호출자를 재측정해
  제품 frame cache scan·Material raw alias/setter·draw pool 중복 writer를 은퇴했다. M6와 D2
  authoring identity 수술에 이어 D5-a는 material resolver 입력과 nil/중복
  model·material·ShaderMeta·texture ID의 cooked 게시 거부를 닫았다. D5-b1은 glTF/FBX
  `sourceKey`와 model sidecar의 저장된 UUIDv4 subasset, 재질별 ShaderMeta resolver,
  CEMF v1 GUID→Derived `.cemc` manifest 계약을 닫았다. D5-b2a는 별도 `AssetCooker`와
  `ModelCookProducer`로 `Prim_Cube`의 실제 sidecar를 CEMC/CEMF에 원자 게시하는 첫 production
  slice를 닫았다. D5-b2b1은 explicit authoring migration으로 현재 checkout의 tracked 11 + local 3
  model에 있는 material 52·embedded
  texture 96 UUIDv4를 전수 재발급하고, CEMC 14 + CEMF 1/entry 66의 결정적 전수 Cook까지 닫았다.
  D5-b2b2는 같은 전수 Cook을 package-base snapshot→Generated/Derived→Merged/Assets→pak으로
  연결하고 `AssetPacker` reopen/index 검증을 닫았다. I5 제품 배선에는 D5-b2c 나머지
  asset producer가 여전히 필요하다.

남은 제품 합류 게이트는 D5-b2c 나머지 식별자 게시와 I5 resolver 배선이다. 아래 M6 항목은 완료
증거로 남긴다.

- [x] SerializationPlan D2가 sidecar 정본, UUIDv4 전수 재발급, atomic authoring/rename과
      scene 14/prefab 9/material 2 authoring corpus를 닫았다.
- [x] SerializationPlan D5-a가 material/texture resolver 입력과 nil/중복
      model·material·ShaderMeta·texture ID의 fail-closed cooked 게시 계약을 닫았다.
- [x] SerializationPlan D5-b1이 모델 sidecar의 실제 material/embedded texture UUIDv4,
      PBR `shaderAssetId` resolver와 GUID-addressed CEMF v1 writer/reader 계약을 닫았다.
- [x] SerializationPlan D5-b2a가 별도 AssetCooker에서 `Prim_Cube` 실제 sidecar identity를
      deterministic CEMC/CEMF로 만들고 새 staging tree를 원자 게시한다.
- [x] SerializationPlan D5-b2b1이 모델 14개 subasset identity 148개를 전수 재발급하고
      두 번 같은 CEMC 14 + CEMF 1/entry 66을 source 변경 없이 게시한다.
- [x] SerializationPlan D5-b2b2가 전수 Cook을 build/AssetPacker/pak에 연결해 Derived tree와
      CEMF를 제품 package에 게시한다.
- [ ] SerializationPlan D5-b2c가 texture/ShaderMeta/scene/prefab Derived producer와 material
      dependency entry를 완성한다.
- [x] MaterialPipelinePlan M6-P2d-c가 generic texture schema/owner vector를 닫았다.
- [x] MaterialPipelinePlan M6-P2d-d가 arbitrary required-asset packet과 canonical seed 제거를 닫았다.
- [x] MaterialPipelinePlan M6-P2d-e가 전체 재질/legacy 전환을 닫았다. P2c의
      실제 legacy Material GUID 선택도 `experiment::Material`의 texture·shader 식별자가
      import→화면까지 보존된 증거는 아니다.

**공동 안전 순서:** 서로 독립인 두 선행 레인을 먼저 닫는다. 모델 레인은
`V2(완료) → V3(완료)`, 머테리얼 레인은 `M5(완료) → M6-P0~P2d-e(완료)`다. D2·D5-a·D5-b1·D5-b2a·D5-b2b1·D5-b2b2는 완료됐고 D5-b2c 나머지 제품 Cook 게시까지 닫힌 뒤
세 레인이 `I5(B 직접 소비)`에서 합류한다. V4 입력 레이아웃 유도는 **I5-D에 묶어
함께 수행하고**(2026-08-29 정정 — 아래 I5 슬라이스 분해 ★ 참조), 그 다음에만 I6
Assimp 은퇴를 수행한다. V2/V3가 생산 소비자 0인 동안 데이터 구조를 바꾸는 것이
하류 렌더 경로를 붙인 뒤 바꾸는 것보다 안전하다.

PHASE 4 `PBR-S3`는 importer 단독 완료 항목이 아니다. `D2/D5-b → I5/V4`가 생산 소비를
연 뒤 metallic-roughness와 함께 현재 `ModelDraft`에서 빠지는 `doubleSided`·
`emissiveStrength`, `TextureSlot`→`TextureReference` 경계에서 빠지는 UV set/offset/tiling/
wrap까지 Material과 Deferred/Forward 양 경로에 도달해야 닫힌다.

### I5. 런타임 어댑터 — **B(치환) 로 확정**

`experiment::Model` 을 렌더러가 소비하는 형태로 잇는다.

| 안 | 내용 | 대가 |
|---|---|---|
| A. 어댑터 | `experiment::Model` → 기존 `::Model` 변환 | 두 표현 유지, 변환 비용 |
| **B. 치환** | 렌더 경로가 `experiment::Model` 을 직접 소비 | 소비자 10파일 수정 |

legacy `Model` 소비 파일은 **10개**로 측정됐다. B 가 가능한 규모다.

**치환의 전제 조건이 붙어 있었다** — "experiment 구조가 현재 구조보다 효율적일
것". 착수 시점에는 둘 다 96B였지만 V2가 `experiment::Vertex`를 **68B**로 줄여
고정 레이아웃 기준 21.42MB → 15.17MB(29.2%)까지 전제를 충족했다. 생산 경로의
legacy `::Vertex`는 여전히 96B다(§1.7).

**트랙 V 가 그 조건을 만드는 경로다.** V3까지 구현돼 정적 48B·스킨 68B의 실제
packed storage가 섰다. 21.42MB → 11.17MB(47.8%)는 기존 233,910정점 감사에 현재
레이아웃을 적용한 전망이고, 대표 정적 자산에서는 0.10MB → 0.05MB를 실행 확인했다.

★ **순서 제약:** V2와 V3를 I5 전에, 생산 소비자 0인 동안 완료했다. 이제
그 뒤 I5-D(V4를 묶는다)에서 입력 레이아웃 5곳·셰이더 4개를 같은 마스크 계약으로 함께 바꾼다.

SceneGraphRedesignPlan(PHASE 16 계열) 과 충돌 여부를 먼저 확인해야 한다.

#### I5-M. `experiment::Material` 정본 승격과 legacy `::Material` 퇴역

I5의 B(치환)는 Model 컨테이너만 새 타입으로 바꾸는 작업이 아니다.
`experiment::Model::materials`의 `experiment::Material`을 영속 저작 정본으로 승격하고,
기존 전역 `::Material`은 전환 기간의 단방향 migration 입력으로만 남긴 뒤 I6에서
제거한다. M6 완료는 기존 렌더 경로의 property·texture·ShaderMeta/PSO 소비 계약을
검증했다는 뜻이지 `::Material` 타입을 장기 존치한다는 뜻이 아니다.

새 기능을 legacy `::Material`에 추가하거나 그 타입을 `MaterialAsset`과
`MaterialRuntimeState`로 다시 분해하지 않는다. 새 경로가 다음 책임을 직접 소유한다.

- `experiment::Material`: D2/D5-b `AssetId`와 ShaderMeta asset ID, 이름 기반 논리
  property, keyword 선택, blend mode의 불변 저작 정본.
- `MaterialInstance`: base material owner와 인스턴스별 override/revision만 소유한다.
  runtime 인스턴스는 asset cache에 등록하거나 독립 `.asset`으로 저장하지 않는다.
- `MaterialResolver`/`MaterialPropertyPacker`: ShaderMeta generation과 reflection layout을
  검증하고 논리 값을 permutation·CB bytes·texture binding으로 해석한다.
  `MaterialPropertyPacker`는 **신규 구현이 아니라 M5-A packing 정본의 이전**이다(아래 선행 작업).
  두 번째 packer를 만들지 않는다.
- `ResolvedMaterial`: 해석한 ShaderMeta/texture generation owner를 보존한다.
- 기존 M6 `EnhancedMaterialDrawSnapshot`: 위 결과를 frame 불변 입력으로 밀봉하며
  Render Thread는 DataSystem이나 mutable material을 다시 읽지 않는다.

asset 복제와 runtime 인스턴스 생성도 분리한다. `DuplicateMaterialAsset`은 catalog가
새 AssetId/.meta를 발급한 새 저작 자산이고, `CreateMaterialInstance`는 MeshRenderer가
소유하는 비영속 override다. 기존 `InstantiateShared`처럼 runtime clone을
`DataSystem::Materials`에 등록하고 원본 `m_fileGuid`를 복사하는 계약은 승계하지 않는다.

**착수 전 실측 (2026-08-29).**

| | 실측 |
|---|---|
| legacy `::Model` 소비 파일 | **13** (계획서의 "10파일"보다 넓다) |
| legacy `::Material` 소비 파일 | **34** |

★ **Material 이 Model 보다 3배 얽혀 있다.** 그런데도 Material 이 먼저인 이유는
`experiment::Model::materials` 가 `experiment::Material` 을 품기 때문이다 — Model 을 치환하려면
그 안의 Material 이 먼저 정본이어야 한다.

★ **첫 게이트 항목은 이미 닫혀 있다.** "resolver 가 D2 identity 를 받아 모든 `assetId`·
`shaderAssetId`·`TextureReference.assetId` 를 채우고 nil/충돌을 게시 전에 거부한다" 는
D5-b2c-1(`CookedModelCodec::Write` publication gate)과 D5-b2c-3(재질 의존 배선)이 이미 한다.

**`experiment::Material` → `EnhancedMaterialDrawSnapshot` 격차:**

| snapshot 필요 | `experiment::Material` | 격차 |
|---|---|---|
| `shaderMetaHandle` | `shaderAssetId`(GUID) | GUID→handle 해석 — **MaterialResolver** |
| `permutationKey` | `keywords`/`keywordSelections` | 축 순서 정규화 |
| `bindingLayout` | 없음 | ShaderMeta reflection 에서 |
| `keywordSelections` | 있음 | 그대로 |
| `propertyBytes` | `properties`(논리 값) | **MaterialPropertyPacker** |
| `textureBindings` | `TextureReference`(GUID) | GUID→texture generation |

★ **packing 정본이 삭제 예정 파일에 갇혀 있다 — M5-A 의 배치 오류다.**
`ApplyDefault`·`ValidateLogicalValue`·`PackProperty` 는 `Material` 상태를 한 글자도 읽지 않고
`ShaderPropertyDesc`·`ShaderMetaPropertyBinding`·`MaterialPropertyValue` 만 받는다. 곧
**Material 클래스의 알고리즘이 아니라 ShaderMeta 계약의 알고리즘**이며 `ShaderMetaReflection`
옆에 있었어야 한다. 그런데 `Material.cpp` 익명 namespace(internal linkage)에 있어 다른 TU 에서
**이름조차 보이지 않는다** — "배제하고 배선한다" 는 선택지가 물리적으로 없고, 남는 건 복사냐
이동이냐 뿐이다.

배치 오류의 이력:

| 날짜 | 커밋 | 사건 |
|---|---|---|
| 08-24 | `72ed27ef` | glTF 임포트 — **Experiment 모델 경계 신설** |
| 08-24 | `4602d128` | M5-A — packing 3종을 `Material.cpp` 익명 namespace 에 작성 |
| 08-25 | `0b3f7f12` | 본 계획서(PHASE 24) 신설 |
| 08-29 | `adc026b4` | `BuildShaderPropertyBlock` 추가 — **주석이 experiment 를 이름으로 부르면서** 같은 루프를 두 번째로 복사, 익명 namespace 는 그대로 |

08-29 시점에 experiment 경계도 계획서도 이미 있었고 주석이 두 번째 소비자를 명시했다. 그
자리에서 정본을 꺼냈어야 했다. 결과로 legacy 안에 같은 루프가 두 벌 있다 —
`ConfigureShaderProperties`(폴백 없음)와 `BuildShaderPropertyBlock`(`MaterialInfo` 3필드 폴백).

또한 legacy 가 이미 이름 기반 논리 property 를 정본으로 쓰고 `MaterialInfo` 는 폴백이다
(`BuildShaderPropertyBlock` 주석: "experiment importer 가 게시한 논리 property 는 언제나 이
fallback 보다 우선한다"). 즉 값 모델이 이미 수렴해 있다.

**선행 작업 — `MaterialPropertyPacker` 분리 (I5 밖) — ✅ 구현·검증 완료 (2026-08-29):**

packing 정본은 I6 에서 `Material.cpp` 가 삭제된 뒤에도 **살아남아야 할 코드**다. ShaderMeta
계약이 남는 한 CB 패킹은 남는다. 그러니 이 분리는 experiment 가 하나도 없어도 I6 을 하려면
반드시 해야 하는 작업이고, **필요를 만든 것은 experiment 배선이 아니라 08-24 의 배치**다.

- 대상: 자유 함수 6종(`NumericElementCount`·`LogicalByteSize`·`FindBinding` +
  `ApplyDefault`·`ValidateLogicalValue`·`PackProperty`)을 `Material.cpp` 밖 ShaderMeta 계약
  쪽으로 이동. 정본은 `(meta, layout, 값 조회) → bytes` 만 진다.
- 존치: `MaterialInfo` 폴백은 legacy 호환 입력이므로 `Material.cpp` 에 남는다.
  `experiment::Material` 은 항상 논리 property 를 가지므로 필요 없다.
- 증명: legacy 두 호출부가 정본을 부르고 기존 게이트가 무변경을 증명한다.
- 귀속: MaterialPipelinePlan M5-A 부채 상환 겸 I6 은퇴 선행. **I5 슬라이스가 아니다.**

★ **이 분리가 서면 I5 는 legacy `Material.cpp` 본체를 한 번도 편집하지 않는다.** experiment 는
include 한 줄로 정본에 붙는다 — 원래 그랬어야 할 모양이다.

**완료 실측 (2026-08-29).** `MaterialPropertyPacker.h/.cpp` 신설(자유 함수 6종 이동, `namespace
MaterialPropertyPacker`), `MaterialPropertyValue`는 `MaterialPropertyValue.h`로 추출 —
packer 헤더가 `Material.h`(Texture.h 등)를 끌지 않고 그 타입을 받기 위한 최소 이동이며
`Material.h`는 include로 표면을 유지한다. `Material.cpp`는 익명 namespace에 using 선언 6줄만
남기고(호출부 무변경, packing 구현 잔존 0건 grep 확정), `MaterialInfo` 3필드 폴백은
`BuildShaderPropertyBlock` 안에 존치. 전 솔루션 Debug x64 빌드 통과,
material-authoring-corpus(2/2)·experiment-cooked-identities·experiment-ft-primitives(실제
DX12 draw 8) 초록.

★ **증명 중에 낡은 게이트 2종이 드러났다 — 이 슬라이스와 무관한 선재 결함이다.**
`verify-experiment-asset-cooker.ps1`·`verify-experiment-model-cook-all.ps1`이
"entries[2].dependencies[0]: dependency GUID가 manifest entry로 해석되지 않는다"로 붉은데,
HEAD 그대로의 baseline 워크트리 바이너리로 같은 명령을 돌려도 **동일하게 실패한다**(A/B 확정).
원인: 두 스크립트는 `--model`만 넘기는데, D5-b2c-3(`aa18b960`)이 재질 entry에
`shaderAssetId` 의존을 배선하고 manifest 폐포 검증이 이를 거부한다 — 게이트 스크립트가
`adc026b4` 이후 갱신되지 않아 계약 변경을 못 따라왔다. 임베디드 texture는 같은 cook 안에서
뽑아 폐포를 닫지만 shader는 외부 자산이라 `--shadermeta` 없이는 원리적으로 못 닫는다.
처방은 게이트가 shadermeta를 폐포에 포함하도록 갱신하는 별도 작업이다(run-all에 둘 다
포함되므로 방치하면 세트 전체가 붉은 채 굳는다).

**→ 해소 (2026-08-30).** 두 게이트에 `--shadermeta GBuffer.shadermeta`를 배선해 폐포를
닫았다. 실측: 14개 모델 재질 52종 전부 GBuffer만 참조한다 — GBuffer 하나로 전수 폐포가
닫히고 Forward 계열 3종은 모델 재질의 참조가 0이다. 다만 재질 cook은 `--shadermeta`와
별개로 asset root 잘 알려진 경로에서 `Forward.shadermeta.meta`를 읽고(shader.forward),
shadermeta cook은 sourceGuid가 가리키는 `GBuffer.hlsl`(+sidecar)까지 검증하므로 relocation·
invalid fixture에 이들을 함께 복사해야 한다. 산출물 단정도 실측으로 갱신
(Prim_Cube: files=4·manifestEntries=4, 전수: files=112·manifestEntries=163). 두 게이트
단독 실행 초록 + `--shadermeta` 제거 변이가 정확히 원래 오류로 붉어짐을 확인했다.

**I5 슬라이스 분해 (2026-08-29, 같은 날 정정):**

| 슬라이스 | 내용 | 소비자 | legacy 편집 |
|---|---|---|---|
| **I5-M1** ✅ | `experiment::Material` 이 정본 packer 로 CB bytes 를 만들고 **legacy 와 비트 단위 패리티** | 게이트(패리티) | **없음** |
| **I5-M2** ✅ | `MaterialResolver`/`ResolvedMaterial` — `shaderAssetId`→ShaderMeta handle, texture GUID→generation owner | **catalog 의 첫 생산 소비자** | 없음 |
| **I5-M3** ✅ | `MaterialInstance` — base + override. `InstantiateShared` 계약을 승계하지 않는다 | MeshRenderer | 없음 |
| **I5-M4** ✅ | sealing 치환 — GBuffer/Forward 가 `experiment::Material` 에서 snapshot 을 만든다 | 제품 렌더 | **소비자** 참조 감소 |
| **I5-M5** | 저작 경계 이전 — MeshRenderer·Scene 직렬화·CLR·Editor picker/Inspector | 제품 저작 | **소비자** 참조 감소 |
| ↳ M5-S0 ✅ | experiment 저작 YAML 코덱(정본 스키마) — 호출부 무변경 | 게이트 | 없음 |
| ↳ M5-S1 ✅ | 변환 정본 + DataSystem 읽기 이중화 — legacy↔experiment 단일 변환기, 새 정본 문서 로드 | DataSystem·sealing 브리지 | 없음 |
| ↳ M5-S2a ✅ | 씬 읽기 경계 — MeshRenderer postLoad가 새 정본 m_Material을 재해석 | 씬/프리팹 로드 | 없음 |
| ↳ M5-S2b ✅ | 씬 writer 전환 — ShaderMeta를 아는 재질은 새 정본으로 저장(legacy 폴백 이중화) | 씬 저장 | 없음 |
| ↳ M5-S2c-1 ✅ | 모델 GUID 자립 — `MeshRenderer::m_modelGuid` 신설, m_fileGuid 편법 이주, InstantiateOwned 비승계 | 씬 스키마·cook 폐포 | 없음 |
| ↳ M5-S2c-2a ✅ | 저작 소유 분리 — base 참조(ref)+diff 저작, 피커 링크·소유 사본 (reflect 퇴출은 2b로) | 씬 스키마·피커 | 없음 |
| ↳ M5-S2c-2b | 런타임 소유 분리 — base(experiment)+`MaterialInstance`, 프록시·sealing 타입 전환 | 프록시 사슬·CLR/Inspector | **소비자** 참조 감소 |
| ↳ M5-S2c-2c | Foliage — `FoliageType.m_material` experiment 전환(비직렬화·저위험) | Foliage·required packet | 없음 |
| ↳ M5-S3 ✅ | CLR API 재구현 — 논리 값 경로, C# ABI 유지 | ClrHost | **소비자** 참조 감소 |
| ↳ M5-S4 ✅ | Editor Inspector — 논리 값 편집·동적 property 편집기·드롭타겟 GUID 정본화 | Inspector | **소비자** 참조 감소 |
| **I5-D** | `experiment::Model` 직접 소비 **+ V4 레이아웃 유도** — 아래 슬라이스로 분해(착수 정찰 2026-08-31) | 제품 렌더 | **소비자** 참조 감소 |
| ↳ I5-D0a ✅ | 선결 갭 ① — 판정: **clip 표현 불요**. looping은 experiment에 이미 완결, 이벤트는 legacy조차 씬(Animator) 소유 — D4에서 Animator 소유 구조로 이관(코퍼스 저작분 0건) | Animator | 없음 |
| ↳ I5-D0b ✅ | 선결 갭 ② — 판정: **표현 불요·기각**. LOD는 소비 0의 죽은 생산 전용 파이프라인(결과 버림+호출자 0+저작분 0건) — 미래 LOD는 렌더 파생 몫 | Mesh·Inspector | 없음 |
| ↳ I5-D1a ✅ | 역브리지 — `DataSystem::BuildLegacyModelFromExperiment`(experiment→legacy) + 왕복 게이트 | DataSystem | 없음(I6 은퇴) |
| ↳ I5-D1b ✅ | 로더 이중화 — `LoadModelGUID`가 experiment(cooked→source) 로드→역브리지 소비, Assimp 폴백·경로 관측 | DataSystem | 없음 |
| ↳ I5-D2 ✅ | V4 유도 정본 — mask→`RHIInputElement` 유도, `RHIFormat::RGBA8Uint` 신설, 전환 계약 게이트 (패스 전환은 D4와 동시로 정정) | RHI | 없음 |
| ↳ I5-D34a ✅ | GBuffer 정적 수직 절단 — 병행 바인딩·캐시 대칭 이중화·VSIn 퍼뮤테이션+PSO 레이아웃 축·A/B 동수 게이트(FT 8/8) | 메시 캐시·GBuffer | 없음 |
| ↳ I5-D34b ✅ | 스킨 전환 — GBuffer·Shadow 스킨 PSO(BLENDINDICES uint4), 스킨 A/B 동수 게이트(10/10 전량·42411 동수), m_Motion 폴백 (WireFrame은 실존 안 함) | 스킨 2패스 | 없음 |
| ↳ I5-D34c ✅ | Forward 전환 — shade/reference×experiment PSO 4벌, core 유도 하나로 마스크 불문, forward 배치 게이트(3c) — SKIN keyword 축은 불요 판정 | Forward | 없음 |
| ↳ I5-D4 | 직접 소비 — DX12MeshCache experiment 정점 업로드, `Model::Shared+MeshIndex` 어댑터(프록시·MeshRenderer·ModelSceneBridge 재작성) + D0a 이관(이벤트·루프를 Animator 소유로) + D0b 절단(postLoad GenerateLODs 재실행 제거) | 렌더 초크포인트 | **소비자** 참조 감소 |
| ↳ I5-D5 | 잔여 소비자 — Foliage/Terrain(중복 패턴 동시)·에디터 패스스루 6파일·CLI, S2c-2b/2c 합류 | 에디터·Foliage | **소비자** 참조 감소 |
| (I6) | `ExperimentLegacyBridge`·legacy runtime codec 은퇴 | — | **본체 삭제** |

**I5-D 착수 정찰 (2026-08-31) — 파급면 3방향 전수.**
① **소비자 분류**: `#include "Model.h"` 24파일 중 오탐 2(Experiment 자체 include 동명 충돌 —
[[substring-match-false-results]]의 재발 유형), 생산자/정의부 2를 제외하면 제품 소비자의
위험도는 극도로 편중돼 있다. **GPU 업로드 초크포인트는 DX12MeshCache::GetOrUpload 하나**
(호출부는 GBuffer/Forward/Shadow 3패스뿐, 96B `sizeof(Vertex)` 고정 가정 + `m_hashingMesh`
캐시 키). 최대 재작성 대상은 ModelSceneBridge(m_nodes 자식 인덱스 순회·이름 조회 —
experiment는 parent만 저장). 저위험 패스스루 6파일(DataSystem 팩토리·GameObjectCommand·
Hierarchy·AssetBundle·ComponentFactory·EditorMain)은 타입 치환만. `GetMeshShared` 개별
shared_ptr 장기 보유 패턴은 experiment 값 타입 모델과 불일치 — `Model::Shared+MeshIndex`
페어 어댑터가 필요하다. Foliage와 TerrainComponent는 같은 패턴의 중복이라 함께 고친다.
② **기능 손실형 갭 둘**(경계 전 필수 판정): 애니메이션 이벤트(`Skeleton::m_animations`의
`AddEvent`/`m_isLoop` — experiment `AnimationClip`에 표현 없음)와 LOD(`GenerateLODs`/
`m_LODThresholds` — experiment Mesh에 없음). D0a/D0b로 분리.
③ **V4 실체**: 입력 레이아웃 5곳은 4파일 5배열(GBuffer 1·Forward 2·Shadow 1·WireFrame 1,
GizmoLine은 자체 정점이라 제외), VSIn 4곳 확정. **마스크 계약은 이미 완성** —
`Experiment/VertexLayout.h`의 `kVertexAttributeTable`이 시맨틱·포맷·permutation 축까지
명시하고 cooked(`vertexLayoutTableHash`·mesh별 mask)로 왕복 중이라 V4는 설계가 아니라
연결이다(mask→`RHIInputElement` 유도 함수 하나, RHI 계층에 — VertexLayout.h는 RHI 타입
금지). 함정 둘: tangent가 RGBA32Float(w=handedness, BINORMAL 없음)라 셰이더가
`bitangent = cross(N,T.xyz)*T.w` 재현으로 바뀌어야 하고, BoneIndices가 RGBA8Uint(legacy는
RGBA32Float — "UINT4로 읽으면 팔레트 밖" 주석의 그 자리)라 스킨 3패스의 형식 변경이
동반된다. 그래서 정적(D2)→스킨(D3) 순서로 격리한다.
④ **Player 배선 현황**: `CookedAssetCatalog`은 완결 구현이나 **제품 인스턴스화 0건**
(self-test만), `ResolvingModelDecoder`는 실디코더지만 실 cooked/source 디코더를 물려 제품에
꽂는 조립이 0, `LoadModelGUID`는 AssetMetaRegistry→Assimp 그대로, Player pak은 cooked가
아니라 소스 트리 사본이다. D1이 이 넷을 잇되 — "생산만 있고 소비 0"을 피하려고 배선과
동시에 `LoadModelGUID`를 이중화한다: experiment 로드 성공 시 legacy 브리지(전환기,
RenderTests의 ExperimentLegacyBridge 승격)로 기존 파이프에 소비시키고 실패는 Assimp 폴백.
화면은 legacy 96B 그대로, 데이터 출처만 experiment — **V2 무손실 픽셀 diff 0 판정을 V4
이전에, 렌더 경로를 열지 않고** 얻는 절단이다(I5-M S1 읽기 이중화와 같은 결). 브리지는
D4에서 걷혀 I6에서 은퇴한다.

**I5-D1a 완료 실측 (2026-08-31).** `DataSystem::BuildLegacyModelFromExperiment` 신설 —
experiment::Model → legacy ::Model **역브리지**(전환기, I6 은퇴). 착수 정정 하나: RenderTests의
ExperimentLegacyBridge는 legacy→experiment **정브리지**(패리티용)라 D1이 가정한 역방향이
없었다 — 신설이 D1의 실비용이었고, 정브리지와 합성하면 왕복 게이트가 즉시 성립하는 구조라
D1a로 분리했다. 시공 견본은 legacy 모델 캐시 로드(LoadSkeleton — 평탄 본 목록+부모 인덱스
트리 재구축). legacy Model/Mesh 컨테이너가 private+friend라 DataSystem 멤버로만 시공 가능해
별도 TU(ExperimentModelMigration.cpp)에 정의했고, Mesh::m_materialIndex는 friend 목록을
편집하는 대신 **reflect() 스키마의 멤버 공개를 순회로 사용**했다(legacy 본체 무편집).
재질은 ConvertToLegacyMaterial 정본 위임. 루트 노드 자기 참조(parentIndex 0)를 자식으로
넣지 않는다 — [[findbone-name-lookup-fails]] 함정의 대우.

게이트 `experiment.modelbridge <경로>` — 정브리지→게시→역브리지 왕복을 원본과 대조(노드·
메시·정점 필드·인덱스·본 remap 반영·클립/채널/키·채널 총량=왕복+dropped). 실측: 정적
Suzanne 10/10(정점 1,066), 스킨 Gunner 21/21(정점 9,391 · dropped 채널 10 = 정브리지의
본 없는 node-anim). 변이(uv1 폴백 파괴)가 양 모델에서 정점 단정 1건만 정확히 붉힘.
실측이 가른 계약 하나: **bitangent 왕복은 handedness(부호) 보존이 계약**이다 — Gunner
원본 bitangent의 74%(6,976/9,391)가 cross(n,t)와 비직교(스킨 가중 평균 산물)라 방향 정확
일치는 원리적으로 불가하고, w 한 비트 재구성의 화면 영향은 D1b 픽셀 게이트가 최종
판정한다(비직교 수는 게이트가 관측 계수로 남긴다). Step 보간은 legacy 표현 부재로 Linear
강등(실측: 현 코퍼스 Step 트랙 전부 상수라 시각 손실 0). 애니메이션 이벤트는 브리지 무관 —
런타임에 Animator가 단다(D0a의 실체는 직접 소비 시점의 표현 판정).

**I5-D1b 완료 실측 (2026-08-31).** 로더 이중화 — `DataSystem::LoadModelGUID`가
`LoadModelViaExperiment`(ImporterModelDecoder+CookedModelDecoder를 ResolvingModelDecoder로
조립 → ModelLoader → 역브리지)를 먼저 타고, 실패는 Assimp 폴백이다. **화면 데이터 출처가
experiment로 바뀌었다**: 씬 코퍼스가 로드한 실모델 10종(스킨 Gunner·SU 포함) 전부
experiment 경로·**폴백 0**, ft-primitives는 8종 experiment 경유로 실 DX12 draw 8건.
정체성 배선: 모델 GUID는 호출자의 .meta GUID, 텍스처는 `resolveTextureAsset`이 원본
경로→registry GUID로 해석(이름 부활 금지)하고, 못 푼 참조만 역브리지가 fallbackPath
파일명을 legacy 이름 필드로 나른다(Finalize 이름 폴백 — 전환기 보강, I6 은퇴).

★ **관측이 설계의 절반이었다.** 성공 무로그+폴백 로그 설계는 "전부 폴백"과 "전부
experiment"가 같은 침묵이 된다(눈먼 초록) — 성공도 stdout 채널(printf)로 관측해 게이트가
경로를 실증한다. 실측: dx12 스윕은 LoadModelGUID를 안 지나는 자가 로드라 이중화에 눈멀고
(무회귀 증거로만 유효), 실증은 ft-primitives·씬 코퍼스 stdout의 [model.dual] 계수가 진다.
**픽셀 판정**: FT 실드로우+스윕 기준선 정확 일치(28·4·2·1)+vk.forward/gbuffer로 정적 경로
diff 0 성립. 스킨 모델은 경로 실증(코퍼스 10종)과 bt-smoke(Animator 실동작 — 역브리지
스켈레톤/클립 위에서)까지 — 스킨 **픽셀** 게이트는 LoadModelGUID를 지나는 것이 없어
한계로 남긴다(D2/D3 V4 게이트에서 함께 닫는 것이 자연스럽다). 잔여: catalog 기동
인스턴스는 cooked 게시 규약(pak)이 서기 전에는 세울 수 없음이 실측 — cookedPath는 빈
경로(resolver Info 계수)이고 배선은 그 규약과 함께 온다. LoadModel(이름 기반)·
LoadCachedModelShared(에디터 배치)는 아직 Assimp — D4/D5에서 수렴.

**I5-D2 완료 실측 (2026-08-31).** V4 유도 정본 — `RHI/ExperimentVertexInputLayout`
(`BuildInputElements`: kVertexAttributeTable에서 시맨틱·포맷·오프셋 전부 유도, 지원 규칙은
`VertexBuffer::IsSupportedLayout` 위임 — 두 번째 정본 금지)과 **`RHIFormat::RGBA8Uint` 신설**
(BoneIndices 대응이 RHI에 없었다 — enum 끝 추가로 기존 값 보존, DX12/Vulkan 매핑·크기·채널
표 동반). 게이트 `experiment.vertexlayout`(합성 22): core 48B/스킨 68B/uv1 밀림의 명시 수치,
fail-closed 3형, **legacy 전환표 단정**(BINORMAL 부재→cross(N,T)*w 재구성·TANGENT RGBA32@32·
BLENDINDICES RGBA8Uint@48·stride 96→68 — D3/D4가 지킬 계약을 게이트에 박음). 변이(RG32→
RGB32 유도 스왑)가 TEXCOORD 단정 2건만 정확히 붉힘. 무회귀: matseal·dx12.forwardshade·
vk.forward/gbuffer.

★ **D2 스코프 정정 — 레이아웃 전환은 정점 버퍼와 동시일 수밖에 없다.** 착수 시 D2에 뒀던
"ForwardShade 전환"은 불가가 실측이다: DX12MeshCache가 96B legacy 정점을 올리는 동안 입력
레이아웃만 유도(48B)로 바꾸면 버퍼와 어긋난다. 유도 정본의 호출부는 아직 게이트뿐이고 그
사실을 숨기지 않는다(S0 코덱 선례). 5곳 레이아웃·VSIn 4곳의 실전환은 D3으로 옮기되 **D4
(정점 버퍼 experiment 전환)와 동시**다 — mesh 단위 이중화(마스크 유도 레이아웃+새 VSIn
퍼뮤테이션 vs legacy 96B 경로)로 좁게 여는 것이 다음 절단이다.

**D3+D4 착수 정찰 (2026-08-31) — 잠금은 두 층뿐이고 소비 층은 이미 중립이다.**
3방향 정찰(메시 캐시·PSO/퍼뮤테이션·픽셀 경로) 종합:
① **소비 층(패스 4곳: GBuffer/Forward/Shadow/WireFrame)은 무변경** — stride는 전부
`RHIMeshBinding::vertexStride` **데이터**로 흐르고(`sizeof(Vertex)` 리터럴 0), draw는
`Mesh*` 키만 나른다. 96B의 실제 발생지는 **DX12MeshCache.cpp:214/287과
VulkanRenderServices.cpp:703/810 — 대칭 4줄**이며 한쪽만 고치면 vk 대조 게이트가
stride 불일치로 붉는다(동시 필수).
② **draw별 레이아웃 스위칭 기구는 이미 있다** — PSO 캐시는 레이아웃 내용 해시
(DX12PSOManager:88·VulkanPipelineCache:174), Forward의 `m_shaderVariants`가 draw별 PSO
선택을 이미 하고(축이 keyword뿐 — 레이아웃 축만 추가), Shadow의 SHADOW_SKINNING이
정적/스킨 듀얼 PSO+배치 정렬+그룹 경계 SetPipeline의 완전한 선례다. VSIn 퍼뮤테이션은
`RHIShaderPermutation::Enable`+`#ifdef`(런타임 DXC, defines별 2단 캐시)로 신규 인프라 0.
③ **첫 픽셀 판정 대상은 GBuffer다** — FT 프리미티브 8건은 전부 deferred(포워드 드로우 0).
Forward-정적 우선안은 실픽셀 게이트가 없어 기각.
④ **experiment 정점의 공급선**: D1b가 역브리지 후 experiment::Model을 버린다 — DataSystem
병행 바인딩(`m_hashingMesh` → {Model::Shared, MeshIndex}, 캐시 키와 동일 축)을 D1b 로드
지점에서 채우고, 메시 캐시가 조회 함수 주입(RHI가 DataSystem을 직접 알지 않게)으로 packed
bytes+stride+마스크를 얻는다. legacy Mesh 본체는 무편집. `RHIMeshBinding`에 마스크 필드
1개를 더해 패스의 PSO 선택 분기가 쓴다.
⑤ 스코프 밖 명시: GizmoLine(자체 정점, 메시 캐시 우회)·MeshOptimizer/ModelLoader 쿡
캐시/model.cache.build(legacy CPU 계층 — 폴백 경로와 함께 I6 판정). 재분해: D34a(GBuffer
정적 수직 절단·FT 픽셀) → D34b(스킨 3패스+스킨 픽셀 게이트 신설) → D34c(Forward+
ShaderMeta 축).

**I5-D34a 완료 실측 (2026-08-31).** GBuffer 정적 수직 절단 — FT 정적 메시 8종의 GPU 정점
출처가 experiment packed(48B)로 바뀌었다(`CREATOR_EXPERIMENT_VERTEX` A/B로 켬 8/끔 0,
커버리지·밝기 동수). 절단선은 정찰대로: ① `RHIMeshBinding::vertexAttributeMask`(stride처럼
데이터로 나름, 0=legacy 96B) ② `IRenderMeshCache`에 조회 주입 계약(`RHIExperimentVertexView`,
**순수 가상** — 두 backend가 갈리면 컴파일이 막음) ③ DX12/Vulkan 캐시 대칭 이중화 +
`experimentUploads` 계수(관측 없으면 "전부 legacy"와 같은 침묵) ④ DataSystem 병행 바인딩
(`m_hashingMesh`→{`Model::Shared`, meshIndex} — 역브리지의 1:1 순서가 키, 계수 불일치면 등록
전체 생략) — **스킨 마스크는 D34b까지 fail-closed**(열면 float4 BLENDINDICES PSO가 팔레트
밖을 짚는다) ⑤ GBuffer.hlsl `EXPERIMENT_STATIC_VERTEX` 퍼뮤테이션(`bitangent =
cross(N,T.xyz)*T.w` — modelbridge의 부호 보존 계약과 같은 결) ⑥ PSO 레이아웃 축: 기본
request와 ShaderMeta variant **전부에 experiment 짝을 함께 생성**(variant 생성 시점에는 어떤
메시가 올지 모른다 — 배치가 메시 마스크로 고름), 레이아웃은 D2 유도 정본에서
(`BuildInputElements(kCoreVertexAttributes)` — 오프셋 재기재 금지) ⑦ Shadow는 **무수술 호환**
(정적 PSO가 POSITION@0 하나 — 두 레이아웃에서 동일)이되 스킨 분류에 마스크 가드, Forward는
D34c까지 fail-closed(스킵+로그 — FT 실측 포워드 드로우 0이라 제품 파급 0).

★ **사고 1 — 블롭 재사용 dangling이 세 층 밖에서 터졌다.** `ApplyShaderMeta`가 experiment
desc를 legacy와 **같은 `vsBlob`으로** 빌드해, legacy desc의 bytecode 포인터가 재할당으로 죽은
메모리를 가리켰고 PSO 생성이 `E_INVALIDARG`. 증상은 "FT 라이브 전멸(드로우 0·광원 0)"이라
원인(GBuffer ShaderMeta 적용 실패)에서 세 층 떨어져 있었다 — 이력 로그의 ERROR 한 줄이
갈랐다. 원인 특정은 **경로 지정 stash 이분**(HEAD 초록 / 작업 트리 빨강 — PHASE 17 동시
커밋을 먼저 의심했으나 무죄). desc는 블롭 내부 버퍼를 빌려 가리킨다 — 같은 변수 재사용 금지.

★ **사고 2 — 관측 지점 오선정.** 첫 게이트는 `dx12.live` status를 관측했는데 `--script`
헤드리스에서 라이브는 **프레임을 완성하지 않는다**(렌더 0프레임 실측 — present 부재).
`dx12.scene` 오프라인 하네스로 옮기고 **하네스 캐시에도 같은 주입**을 걸었다 — 안 걸면
하네스가 전부 legacy로 그려 "라이브 배선의 회귀 감시자"가 experiment 경로에 눈먼다.

★ **사고 3 — 변이 2연속 거짓 초록, 이빨은 A/B 동수 단정에 있었다.** ① 마스크 무시(legacy
PSO+experiment 버퍼)는 초록 — position/normal/uv 오프셋이 두 레이아웃에서 **동일**(0/12/24)
하고 FT 재질에 노멀맵이 없어 tangent 파손이 원리적으로 안 보인다(→ **D34b 픽셀 게이트가
노멀맵 재질을 포함해야 하는 이유**). ② POSITION 오프셋 +12는 커버리지 36706→2245로 그림이
붕괴했는데도 초록 — 하네스 커버리지 단정이 "0이 아니다" 수준이었다. 게이트에 **on/off
커버리지 동수(4d)·밝기 동수(4e)** 단정을 신설하자 ②가 정확히 붉었다(같은 씬·같은 카메라를
두 경로로 그리므로 동수가 정의상 성립 — 언팩이 float 비트 보존이라 밝기도 문자열까지 동일).

게이트: `verify-experiment-vertex-live.ps1` 신설(run-all 편입) — [model.dual](로드)와
experiment 업로드 계수(GPU)를 분리 관측 + A/B 동수. 남김: 정적 픽셀 diff·노멀맵 축은 D34b
스킨 픽셀 게이트와 함께, Forward 전환은 D34c.

**I5-D34b 완료 실측 (2026-08-31).** 스킨 전환 — 스킨 메시(Gunner, 68B packed,
BLENDINDICES **RGBA8Uint@48**)가 GBuffer/Shadow의 experiment 스킨 PSO로 그려진다. 게이트
실측: 업로드 10/10 전량 experiment(N==M 단정 — 스킨 전용 계수 없이 전량으로 가른다),
A/B 커버리지 42411 동수(Gunner가 화면 기여 ~5,700픽셀), 변이(스킨 유도 레이아웃
POSITION+12)가 4d·4e를 정확히 붉힘. 절단선: ① DataSystem lookup의 스킨 거부 해제 ②
`LoadModel`(이름 경로)도 experiment 이중화 — **D4 로드 수렴의 절반 선취**(CLI model.load가
legacy 직행이라 스킨 게이트가 성립하지 않던 갭) ③ 셰이더는 **VSIn 선언만 분기**
(`EXPERIMENT_SKINNED_VERTEX` — uint4 boneIndices; 본문의 `(uint)` 캐스트가 양쪽에서
항등이라 수식은 한 벌) ④ GBuffer 레이아웃 축 3종(legacy/expStatic/expSkinned — 기본
request·variant 전부 짝 생성, 마스크→매크로·레이아웃을 BuildPipelineDesc가 유도) ⑤
Shadow는 experiment 스킨 PSO 하나 추가(유도 6원소 전체 선언 — VS가 읽는 시맨틱만
조립되므로 부분집합 재기재 금지), 분류는 "팔레트 && 버퍼가 스킨 어트리뷰트 보유"로 ⑥
WireFrame은 **실존하지 않는다**(정찰 표기 정정 — RHIInputElement 발생지는 3패스뿐).

★ **부수 결함 두 건을 게이트가 잡아 고쳤다.** ① 저장·재로드된 스킨 씬이
`AnimationJob::UpdateBone` 널 역참조로 죽었다(0x80) — 원인은 역브리지의 `m_Motion`이
nil이라 Animator postLoad가 스켈레톤을 복원하지 못하고 **reflect가 만든 빈 스켈레톤(본
0)**이 틱에 나간 것. 3점 계측(역브리지 fail-closed / postLoad 관측 / 틱 가드 — "본
0개" 로그가 원인을 갈랐다)으로 특정하고, 역브리지가 m_Motion을 모델 guid로 폴백하게
고쳤다(Assimp 경로와 같은 의미론). 가드·계측은 존치 — 스켈레톤 재구성 경로의 상비
진단이다. ② CLI `object.transform`은 라이브 프록시에 전파되지 않는다(실측 — 스케일
0.01로도 화면 불변, [[proxy-update-fanout-and-dirty]]의 T4 미연결과 정합) — 게이트는
저작값 기록 후 **저장·재로드**로 반영한다.

★ **게이트의 결정성 한계(정직).** 애니메이션 틱은 실시간(deltaTime) 기반이라 포즈가
실행마다 다르다 — A/B 동수 단정과 양립하지 않아 게이트는 저장 전에 Animator를 끄고
**바인드 포즈**로 판정한다. 정점 페치(BLENDINDICES uint 조립)는 스키닝 분기와 무관하게
PSO 레이아웃대로 일어나므로 레이아웃·업로드·PSO 선택 판정에는 손실이 없지만, **스키닝
산술(팔레트 인덱싱)의 시각 판정은 이 게이트 밖이다** — 합성 검증(dx12.skinning)이
그 축을 쥐고 있고, 결정적 포즈 고정(시간 주입) 없이는 실씬 판정이 서지 않는다.

**I5-D34c 완료 실측 (2026-08-31).** Forward 전환 — D34a의 fail-closed를 걷고 forward 큐가
experiment packed 버퍼로 그려진다. 게이트 실측: `render.matmode`로 P_Torus를 투명으로 바꿔
forward 큐를 실제로 채우고(그 커맨드의 존재 이유 그대로 — "이 값 없이는 Forward+ 경로를
실씬에서 실행해 볼 수 없다"), 포워드 큐 1·**배치 1** on/off 동수. 절단선: ① 레이아웃 축은
**core 유도 하나로 마스크 불문**(Forward는 본을 안 읽고 48B 프리픽스가 core/skin 마스크에서
동일 — stride는 바인딩) ② shade/reference 짝 × experiment = **PSO 4벌**(기본
`CreatePipelines`와 ShaderMeta `Apply`/`Ensure` 전부 — Apply가 기본 PSO를 ShaderMeta
경로로 **교체**하므로 두 생성 경로 다 짝이 필요하다) ③ ForwardShade.hlsl은
`EXPERIMENT_STATIC_VERTEX` 분기(bitangent cross 재구성 — GBuffer와 같은 계약) ④ 계획의
"ShaderMeta SKIN keyword 축"은 **불요로 판정** — GBuffer가 스킨을 인스턴스 분기+마스크
PSO로 처리하고 Forward는 본을 안 읽어, keyword 축을 세울 소비자가 없다.

★ **변이 3연속이 관측의 세 구멍을 차례로 드러냈다.** ① 기본 PSO 유도 훼손 → 초록:
`ApplyShaderMeta`가 기본 PSO를 ShaderMeta 경로로 교체해 **죽은 경로를 훼손한 것**이었다.
② 실경로(ShaderMeta) 유도 훼손 → 여전히 초록: 하네스의 커버리지는 **깊이 리드백**인데
forward는 `depthWrite=Zero`, 밝기는 **deferred 라이팅 리드백** — 두 관측축 모두 forward
출력에 **원리적으로 눈멀다**. ③ 배치 소실 변이 → "포워드 드로우 1(발행 1 · 배치 0)"이
드러남 — 하네스에 실발행 계수를 추가하니 "발행"(GetLastDrawCount)조차 배치 이전의 큐 순회
계수였고, **배치 계수만이 판별력**이 있었다. 게이트 3c(큐>0인데 배치 0이면 붉음)를 그
계수로 세우자 ③이 정확히 붉었다. 한계(정직): **forward 출력의 시각 판정(픽셀·밝기)은 현
관측 표면에 없다** — forward 합성 결과 리드백 판정은 후속 몫이고, 그때까지 forward 시각
축은 합성 검증(dx12.forwardshade — legacy 자가 지오메트리)만 쥔다.

**I5-D0a·D0b 완료 판정 (2026-08-31).** 착수 정찰이 "기능 손실형 갭 둘"이라 적은 항목의
전수 실측 — **둘 다 experiment 표현이 필요 없다**는 판정으로 닫힌다. 구현 0.

**D0a(애니메이션 이벤트·루프): clip 표현 불요 — 소유는 씬(Animator)이고 legacy가 이미
그렇다.** ① `looping`은 갭 자체가 아니었다: experiment `AnimationClip.looping`이 이미 있고
(ModelData.h) cooked 왕복(CookedModelCodec)·역브리지 왕복(D1a 게이트 m_isLoop 대조)까지
완결이다. ② `KeyFrameEvent`는 **legacy에서조차 모델 자산에 직렬화되지 않는다** — ModelLoader
캐시 포맷은 `m_isLoop` 다음이 바로 nodeAnimCount(이벤트 없음). 유일한 영속은 씬 YAML:
writer는 리플렉션 사슬(Animator→Skeleton::reflect(m_animations)→Animation::reflect(m_isLoop·
m_keyFrameEvent))이 자동으로 쓰고, reader는 Animator postLoad 수동 복원(Animator.cpp:517-574)이
**공유 자산 `m_Skeleton->m_animations`에 재주입**한다. 발화 소비는 살아 있다(`InvokeEvent
(Animator*)` → CLR 스크립트 큐 — 무인자 오버로드는 주석 시체). ③ 따라서 D4 계약: experiment
모델은 이벤트를 모르는 것이 옳고, **Animator가 이벤트·루프 오버라이드를 자기 소유 구조로
이관**해 재생 시점에 결합한다 — 표현 추가가 아니라 소유 이동이며, 공유 자산 재주입(같은
스켈레톤을 공유하는 Animator 간 이벤트 오염 — 마지막 로드 승자)이라는 기존 결함도 그때 함께
청산된다. 에디터(ImGuiDrawHelperAnimator — 자산 Animation 직접 편집)의 편집 표면 전환은 D5.
④ 데이터 이주 부담 **0**: 코퍼스 전수에서 `m_keyFrameEvent` non-nil 0건 · `m_isLoop: false`
0건(전부 nil/기본값 — 씬 스키마에 필드만 나가고 저작된 적 없음).

**D0b(LOD): 표현 불요·기각 — 갭이 아니라 이미 죽은 생산 전용 파이프라인이다**
([[plan-target-may-be-already-dead]]·[[dead-produce-only-pipeline]]의 재현). ①
`Mesh::GenerateLODs`는 MeshOptimizer 실계산을 돌리고 **결과 인덱스 버퍼를 버린다** —
`LODResource`는 `{uint32 indexCount}` 하나뿐이고 단순화된 인덱스는 어디에도 저장되지 않는다.
② 그 계수(`m_LODs`)의 유일한 독자 `HasLODs`는 전 리포 호출자 **0**. 소비 사슬(DrawLOD 계열
8종·PrimitiveRenderProxy InitializeLODs/GetLODLevel)은 DX11 은퇴와 함께 이미 제거됐다(양쪽
파일 주석이 자백). 살아 있는 것은 저작 껍데기뿐: 씬 `m_LODThresholds` 저장(Mesh::reflect) →
MeshRenderer postLoad가 GenerateLODs 재실행(MeshRenderer.cpp:372-380 — 버려질 결과를 매
로드마다 계산) → 에디터 버튼. ③ 판정: experiment Mesh/cooked에 LOD 표현을 **넣지 않는다**.
미래 LOD는 cooked 자산이 아니라 렌더 파생(DX12 캐시 계층)의 몫. ④ 데이터 이주 부담 **0**:
코퍼스 전수 `m_LODThresholds` non-nil 0건. D4에서 legacy Mesh가 걷힐 때 postLoad 재실행을
끊고 thresholds는 스키마 호환만 유지(nil 통과)하면 된다.

교훈: 착수 정찰의 "기능 손실형 갭" 분류는 **API 표면**(AddEvent·GenerateLODs가 존재한다)을
보고 내린 것이었다 — 표면 뒤의 소비 사슬과 실저작 데이터를 전수하면 하나는 이미 닫힌 갭
(looping)+소유 이동 명세(이벤트)였고, 하나는 시체였다. 판정 슬라이스의 실비용은 구현이
아니라 "누가 소비하고 데이터가 실존하는가"의 전수다.

★ **V4는 I5-D에 묶는다 (2026-08-29 정정).** 입력 레이아웃 5곳과 셰이더 `VSIn` 4곳은 legacy
`::Vertex`를 전제하므로, 렌더 경로가 `experiment::Model`을 직접 소비하기 시작하는 I5-D가 그
전환의 유일한 자리다. 따로 떼면 어느 쪽이든 어긋난다 — I5-D 뒤로 미루면 같은 렌더 경로
10파일을 두 번 열고(위험 표 "I5 치환 뒤 레이아웃 변경 비용 급증"), 앞으로 당기면 소비자 0인
유도 코드가 생긴다. V3가 미뤄 둔 픽셀 차이 0 판정(§4-V V3)도 I5-D 게이트에서 함께 닫는다.

★ **치환 트랙이 편집하는 것은 소비자이지 legacy 본체가 아니다.** M4/M5/D 가 건드리는 것은
`::Material`·`::Model` 을 **부르는** 파일들이고, 편집 방향은 legacy 참조를 지우는 쪽이다.
`Material.cpp` 본체는 트랙 내내 편집되지 않고 I6 에서 통째로 삭제된다. 이 방향이 뒤집히는
편집(legacy 를 존치한 채 experiment 와의 공유 의존을 추가하는 것)이 나오면 그것은 I5 의 일이
아니라는 신호다 — 위 선행 작업이 그 예다.

★ **같은 유형의 두 번째 사례 — `Model.h` 패리티 getter (2026-08-29 오염 감사 발견).** 위
불변식을 어긴 편집이 `Material.cpp` 하나가 아니었다. `72ed27ef`(Experiment 경계를 신설한 바로
그 커밋)가 legacy `Model.h` 본체(43-46행)에 "Experiment 패리티 검증(RenderTests)이 구조를 읽기
위한" 주석과 함께 `GetNodes`·`GetMeshCount`·`GetMaterialCount` 3종을 추가했다 —
`private`+`friend`(ModelLoader·DataSystem)로 막혀 있던 필드에 공개 표면을 연 legacy 본체
편집이다. 게다가 주석의 근거가 부정확하다: 실소비자는 ExperimentParity 4파일 외에 **experiment
와 무관한** legacy 검사 `Cmd_model_cache_build`(ConsoleCommandSystem)도 있다.

처방은 `Material.cpp` 와 다르다. packing 정본과 달리 이 getter 는 I6 에서 살아남을 이유가 없는
코드다 — 소비자 양쪽(`ExperimentLegacyBridge`, `Cmd_model_cache_build`)이 전부 legacy `::Model`
과 함께 은퇴한다. 그래서 **선행 분리가 필요 없다**: 지금 되돌리면 브리지와 legacy 검사가 같이
깨지므로 존치하고, I6 에서 legacy `::Model` 은퇴와 함께 삭제한다. 주석 정정도 별도 커밋으로
만들지 않는다 — legacy 본체 편집 금지는 사소한 정정에도 똑같이 적용되고, I6 삭제가 오류를
통째로 해소한다. 이 문단이 기록하는 것은 처방보다 **유형**이다: 두 사례 모두 "experiment 를
위해서"라는 명목으로 legacy 본체에 표면을 추가했고, 세 번째 사례가 나오면 그 커밋은 방향이
뒤집힌 것이다.

**I5-M1 완료 실측 (2026-08-29).** `Experiment/MaterialPropertyBlock.h/.cpp` 신설 —
`experiment::BuildMaterialPropertyBlock`이 정본 packer 4함수를 그대로 부르고, 변환
(`TryConvertMaterialProperty`)은 fail-closed다: desc.type과 variant 대안 불일치(float 자리의
문자열·int 자리의 uint32·표현이 없는 Float4x4 저작값 등)는 기본값으로 덮지 않고 실패한다.
legacy `Material.cpp` 편집 0. 게이트 `experiment.matparity`(합성 51 · 실사 9) 신설 — 합성 leg는
손으로 짠 meta/layout으로 타입 8종 전부를, 실사 leg는 `ShaderMetaFixture.shadermeta`의 실제
Slang reflection layout 위에서 대조한다. 알려진 경계를 단정으로 못박았다: 저작값 부재 시 legacy
MaterialInfo 3필드 폴백과 experiment의 ShaderMeta 기본값은 **달라야 하고**, 그 다름까지 게이트가
잰다(폴백 비승계가 계약이다). 변이 증명: Float4 변환의 z/w 스왑이 저작값이 흐르는 패리티 3건만
정확히 붉히고 기본값 경로는 초록으로 남았다 — 게이트가 재는 것이 변환 데이터 모델임을 확인.
name collision 함정 하나를 기록한다: `experiment::MaterialPropertyValue`는 variant 별칭이라
namespace 안에서 packer의 논리 값 struct는 `::MaterialPropertyValue`로 한정해야 한다.

**I5-M2 완료 실측 (2026-08-29).** `Experiment/MaterialResolver.h/.cpp` 신설 —
`ResolveMaterial`이 shaderAssetId→handle+generation(GUID 일치 검증), keyword 정규화(이름이
정본으로 인덱스를 덮는다 · 모호/미지/범위 밖 fail-closed), texture GUID→generation owner
(cooked 우선·source 폴백, 폴백은 notes 계수로 관측 가능)를 해석한다. legacy
`FinalizeMaterialRuntime`의 이름 폴백은 승계하지 않고(nil assetId만 "텍스처 없음"),
compress 결정은 legacy 패리티(baseColorMap만)로 두고 colorSpace 승격은 M4 픽셀 대조와 함께
판정한다. Experiment 경계 유지 — DataSystem 싱글톤 미접근, 서비스 주입. 제품 바인딩은
`ExperimentMaterialResolveBinding`(root)이 DataSystem 정본+`CookedAssetCatalog::
ResolveArtifactPath`에 잇는다 — catalog 소비 코드는 이제 제품에 있고, **catalog 인스턴스를
기동 경로에 실제로 세우는 것은 I5-D(Player 배선)의 몫**이다. 게이트 `experiment.matresolve`
(합성 43 · 실사 7): 가짜 서비스 호출 계수로 cooked/source 순서를 재고, 실사 leg는 제품
바인딩이 실제 DataSystem 위에서 ShaderMetaFixture를 해석한다. 변이 증명: cooked 조회를
건너뛰는 변이가 cooked 우선 단정 5건만 정확히 붉혔다.

**I5-M3 완료 실측 (2026-08-30).** `Experiment/MaterialInstance.h/.cpp` 신설 — base 저작 정본의
공유 스냅샷 + 이름 기반 property override(같은 이름 갱신, 축적 금지) + keyword override
(base 뒤에 덧붙어 resolver의 순서 규칙으로 같은 축을 이긴다) + revision(Set/Clear마다 증가,
M4 sealing의 무변경 스킵용). texture 교체도 TextureReference 값 override로 표현되므로 별도
슬롯 API가 없다. `BuildEffectiveMaterial`은 base를 변형하지 않는 완전 소유 사본이고, base
identity는 shader·texture **해석용**으로만 보존된다 — 등록(`DataSystem::Materials`)·저장
경로가 타입에 존재하지 않는 것이 `InstantiateShared` 비승계의 실체다. 게이트
`experiment.matinstance`(합성 36): base 불변성·override 합성·resolver 통합(override off가
base on을 이김)·**인스턴스 경로 CB bytes가 직접 저작과 비트 단위 동등**(두 번째 packer가
아니라는 증명). 변이 증명: override를 갱신 대신 중복 축적으로 바꾸는 변이가 4건(덮기·개수·
재설정·bytes)만 정확히 붉혔다. MeshRenderer 실배선은 I5-M5의 몫이다.

**I5-M4 완료 실측 (2026-08-30).** 착수 전 전수 조사로 sealing의 legacy 소비가 seal 지점 2곳
(Forward `trySeal`·GBuffer)과 공용 `SealMaterialTextureBindings` 하나뿐임을 확정했다 —
Water/Wind/Foliage는 별도 sealing이 아니라 같은 Forward 경로를 지나는 ShaderMeta 변형이고,
Foliage material도 동일 코드로 밀봉된다. `Render/Scene/ExperimentMaterialSealing` 신설:
`BuildSealSourceFromLegacy`(legacy → `SealSource` 변환 브리지, **MaterialInfo 3필드 폴백을
여기서 승계** — sealing 교체가 bytes를 바꾸지 않게. 브리지는 I6에서 legacy와 함께 은퇴) +
`SealCore`(정본 packer 경유 propertyBytes + reflection register 검증 texture bindings).
두 seal 지점은 이제 legacy를 **변환 한 번**만 읽고, keyword 정규화(M2)·packing(M1)은
experiment 정본을 탄다. legacy `SealMaterialTextureBindings`는 소비자 0이 되어 삭제
(소비자 참조 감소의 실물).

★ **픽셀 게이트가 폴백 경로를 못 밟는다 — 변이가 들켰다.** 브리지의 baseColor 폴백을 0으로
망가뜨려도 dx12.forwardshade·vk.gbuffer가 초록이었다 — 게이트 재질들이 전부 논리 값을
저작해서 MaterialInfo 폴백이 안 걸린다(저작 자산이 생성 경로 결함을 가리는 그 형태).
`experiment.matseal`(브리지 단정 20)을 신설해 저작·폴백·기본 생성 재질 3형의 legacy 대
브리지+SealCore 바이트 패리티를 직접 잰다 — 같은 변이가 정확히 폴백·기본 2건만 붉힌다.

남는 legacy 읽기와 격차 기록: draw 분류(`m_renderingMode`)와 `BuildRequiredAssetPacket`의
`m_shaderMetaGuid` 읽기는 packet 구축 표면이라 I5-M5/D의 몫. `m_flowInfo`·MaterialInfo
스칼라 4종은 experiment 데이터 모델에 표현이 없어 `SealSource`의 부속 필드로 나른다 —
PBR-S3/I5-M5의 논리 property 승격 후보. 구조적 비대칭 하나를 조사가 드러냈다: GBuffer
snapshot에는 flow 필드 자체가 없어 opaque로 그려지는 wind 재질은 wind 애니메이션을 원리적으로
못 받는다(legacy 시절부터의 형태 — 이번 치환은 보존).

검증: 전 솔루션 Debug x64 빌드, `experiment.matseal` 20 · matparity/matresolve/matinstance
전수 초록, dx12 스윕 기준선 일치(통과 28·완료 4·실패 2·무판정 1), dx12.forward/forwardshade,
vk.gbuffer/forward/grid, experiment-ft-primitives(실 DX12 draw 8), material corpus 2/2.

**I5-M5 착수 실측 (2026-08-30).** 저작 경계의 legacy 소비를 전수 조사했다. 4표면:
① MeshRenderer(7파일) — `shared_ptr<Material> m_Material`이 reflect 스키마 첫 필드라 씬
YAML에 재질 전 필드가 **인라인**으로 실리고, `OnDeserialized`가 `FinalizeMaterialRuntime`과
`m_fileGuid`→모델 GUID 재사용 편법에 의존. ② Scene 직렬화/DataSystem(5파일) — Serialize/
DeserializeMaterialPayload(YAML)·바이너리 페이로드·Load/Insert/FindCached/SnapshotMaterials
전부 legacy 시그니처. ③ CLR(2파일) — API 6종, `TrySetValue`(CB 이름 의존)와
`m_materialInfo.m_baseColor` **필드 직접 접근**이 가장 깊은 결합. C# ABI(P/Invoke 시그니처)는
유지 가능 — C++ 구현만 바꾸는 절단선. ④ Editor(8파일) — Inspector가 MaterialInfo 고정
필드를 ImGui에 직접 바인딩, TextureDropTarget이 `Use*Map` 슬롯 API 사용, 피커는
`SnapshotMaterials` 결합, undo/redo가 **이름 기반 재조회**에 의존. `InstantiateShared` 실
호출부는 2곳(Inspector·CLR)뿐이다.

하위 슬라이스: **S0(코덱) → S1(DataSystem 이중화) → S2(MeshRenderer 소유 전환) → S3(CLR)
→ S4(Editor)**. S2가 병목(스키마 변경 + SceneManager/App 동시 파급)이고, Foliage는
비직렬화 필드라 S2에서 분리 가능한 저위험 조각이다.

**M5-S0 완료 실측 (2026-08-30).** `Experiment/MaterialAuthoringCodec.h/.cpp` 신설 —
experiment::Material 저작 YAML 정본 코덱(schema 1). 값은 타입 키가 명시된 단일 키다
(`float/float2/float3/float4/int/uint/bool/string/texture`) — legacy 4필드 병존 표기와 달리
meta 없이 왕복 가능하다. texture는 `{guid, colorSpace}`만 저장하고 logicalName/fallbackPath는
저장하지 않는다 — D5-c가 죽인 이름 참조를 정본에 되살리지 않는다. shaderAssetId는 canonical
UUIDv4 필수, assetId는 인라인 재질을 위해 nil 허용. fail-closed: 비정규 GUID·값 키 0/2개·
미지 키·비정규 blendMode 전부 거부. 게이트 `experiment.matcodec`(합성 40 — 변이 대안 9종
왕복·골든·fail-closed 12형) — float3 성분 스왑 변이가 왕복 1건만 정확히 붉혔다. 호출부는
아직 0곳이고 그 사실을 숨기지 않는다(소비는 S1의 몫).

**M5-S1 완료 실측 (2026-08-30).** `ExperimentMaterialMigration.h/.cpp` 신설 — legacy ↔
experiment 변환의 **단일 정본**. `ConvertLegacyMaterial`(meta 선언 기준 변환 + MaterialInfo
3필드 폴백 승계)과 `ConvertToLegacyMaterial`(전환기 어댑터 — string property·int32 밖 uint
fail-closed, 이름 keywords는 meta 필수 정규화, colorSpace는 legacy 표현 부재로 소실을 명시,
baseColor/metallic/roughness를 m_materialInfo에 역동기화). M4 sealing 브리지가 자기 변환
루프를 지우고 여기에 위임한다 — 변이 하나(metallic 폴백 제거)가 matmigrate와 matseal을
**동시에** 붉혀 두 번째 변환기가 없음을 증명했다. `DataSystem::DeserializeMaterialPayload`
읽기 이중화 — 새 정본 문서(schema+shaderAssetId)를 experiment 코덱으로 읽고 legacy 런타임
재질로 변환하며, 이름 keywords는 실제 ShaderMeta를 로드해 정규화한다(짐작 금지). census의
"캐시 API 재구현"은 하지 않았다 — `DataSystem::Materials`는 I6에서 죽는 표면이라 재구현이
아니라 은퇴 대상이고, S2/S4가 필요로 하는 것은 이 변환·읽기 경계다. 제품 writer의 새 정본
전환은 S2(씬 포맷 이주)와 함께 판정한다. 게이트 `experiment.matmigrate`(합성 17 · 실사 5):
legacy→experiment→YAML→experiment→legacy 전체 사슬의 **CB bytes 비트 패리티**, 폴백
승계·역동기화, DataSystem 실사(새 정본 로드 + legacy 문서 경로 무변경). 함정 기록:
`::Material`은 복사/이동 대입이 삭제돼 있어 변환 결과는 필드 단위로만 커밋해야 한다(성공
확정 후 쓰기 — 부분 출력 금지).

**M5-S2a 완료 실측 (2026-08-30).** MeshRenderer::OnDeserialized 이중화 — m_Material 노드가
새 정본(schema+shaderAssetId)이면 S1 경로로 재해석하고, legacy 노드는 typed가 채운 인스턴스를
보존한다. 게이트는 matmigrate 실사 leg 확장(9 단정) — postLoad 재해석·legacy 보존, 감지
비활성 변이가 정확히 1건을 붉혔다. 씬 코퍼스 14개 load→save→reload 바이트 안정(28/28)과
ft-primitives 무회귀 확인.

★ **구조 발견 — reflection의 shared_ptr 멤버는 타입 단위 후킹이 불가능하다.** Material에
TypeOps를 재등록해 전역에서 가로채려 했으나, `EmitMember`/`ReadMember`는 Component-정확
원소만 레지스트리로 디스패치하고 그 외 reflectable pointee는 **컴파일 타임 재귀**로 직렬화한다
(ReflectionTypedYml.h). 인라인 재질의 전환 절단선은 리플렉션이 아니라 **소비자 postLoad**다 —
S2c(reflect 스키마 이주)까지 이 제약이 유지된다.

★ **S2b(writer 전환)를 보류한 이유 — 지금 뒤집으면 저작 데이터가 소실된다.** 새 정본은
`m_flowInfo`(windVector/uvScroll — Water/Wind/Foliage의 정본 값), `m_useNormalMap`, `m_IOR`,
고정 5슬롯 텍스처 이름을 표현하지 못한다. flow의 올바른 종착은 ShaderMeta 논리 property 승격
(Water/Wind shadermeta가 windVector·uvScroll을 선언하면 특수 필드가 사라진다 — PBR-S3와 같은
결)이고, 그 전에 writer를 새 정본으로 바꾸면 저장할 때마다 flow 저작이 조용히 사라진다.
S2b는 그 표현이 선 뒤에만 연다.

**M5-S3 완료 실측 (2026-08-30).** `SceneRuntime/MaterialScriptBinding.h/.cpp` 신설 — CLR
property API의 논리 값 경로. legacy `TrySetValue`의 두 족쇄(RuntimeSchema 설치·CB 이름
일치)를 걷었다: 검증 기준은 ShaderMeta 선언(desc.type)이고 갱신 대상은 이름 기반 논리 값이다.
M4 이후 sealing이 매 프레임 논리 값에서 재pack하므로 논리 값 갱신이 곧 화면 갱신이다 —
완료 게이트의 "runtime CB 이름/RuntimeSchema에 기대지 않는다" 충족. `GetBaseColor/SetBaseColor`
의 `m_materialInfo` 직접 접근을 논리 값 우선·사본 동기화로 치환. `InstantiateOwned`가 CLR의
`InstantiateShared` 호출을 대체 — 클론을 asset cache에 등록하지 않고(비승계) 수명은
MeshRenderer shared_ptr가 진다. ★ **m_fileGuid는 아직 승계한다** — MeshRenderer가 mesh 해석에
material fileGuid를 재사용하는 족쇄(S2c에서 분리) 때문에 지금 지우면 씬 재저장 후 재로드에서
메시가 사라진다. C# ABI(P/Invoke)는 무변경 — ClrHost 구현만 교체(buffer 인자는 ABI 유지용,
검증하지 않음을 명시). `InstantiateShared` 잔여 호출부는 Inspector 1곳(S4)뿐이다. 게이트
`experiment.matscript`(합성 25 · 실사 4): schema 없는 갱신·오타/타입 fail-closed·스칼라
동기화·비등록 클론·원본 불변. 변이(타입 검증 제거)가 정확히 2건을 붉혔고, BT 스모크로 CLR
부팅 경로 무회귀 확인.

**M5-S4 완료 실측 (2026-08-30).** Inspector(ImGuiDrawHelperMeshRenderer)의 편집 경로를 논리
값으로 전환했다 — UI는 얇게, 로직은 전부 게이트된 MaterialScriptBinding에 둔다(신규
`SetFloatVector`/`GetFloat`/`SetTexture`, matscript 합성 35로 확장·변이 증명). ① MaterialInfo
스칼라 직접 바인딩 → 논리 값 경로(meta 없는 legacy 재질만 사본 직접 쓰기 폴백, IOR은 legacy
전용이라 유지). ② "Shader Properties" 동적 편집기 신설 — ShaderMeta 선언 순회, 타입별 위젯,
부재 값은 정본 packer의 ApplyDefault로 기본값 표시(0을 보여주면 기본 1.0 저작이 틀리게 보인다).
③ TextureDropTarget 3중복 블록을 헬퍼로 통합 — 드롭의 정본은 GUID 논리 값이고 이름 필드는
더 쓰지 않으며, .meta GUID 없는 드롭은 거부한다(조용한 소실보다 거부). delete는 GUID·이름을
함께 비운다(이름만 남으면 Finalize 이름 폴백이 텍스처를 되살린다). ④ Instantiate 메뉴가
`InstantiateShared`의 마지막 호출부였다 — `InstantiateOwned`로 교체(비영속·캐시 비등록,
SaveMaterial 즉시 저장 제거). **`InstantiateShared` 제품 호출부는 이제 0이다.** ⑤ 피커 undo의
이름 재조회(FindCachedMaterial)를 이전 shared_ptr 직접 캡처로 교체 — 캐시에 없는 인스턴스로도
되돌린다. 미이관 잔여: 피커 열거(SnapshotMaterials)·SaveMaterial writer는 legacy 유지(S2b/S2c/
I6과 함께), AssetBundle/ResourceCounter/Terrain 경량 표면은 시그니처 무변경. 검증: 빌드,
matscript 35+4·matmigrate, material corpus 2/2, 씬 코퍼스 28/28. Inspector 시각 동작은 CLI
게이트 밖이다 — 에디터 실사용 확인이 남는다.

**flow 승격 완료 실측 (2026-08-30).** `m_flowInfo`(windVector/uvScroll)를 ShaderMeta 논리
property(`flowUvScroll`·`flowWindVector`)로 승격했다 — S2b의 최대 선결 해소. Forward/Water/
Wind shadermeta 3종과 ForwardShade.hlsl의 b2 CB에 공통 선언을 더했고, snapshot draw는 CB
저작값을, legacy/self-test draw는 기존 인스턴스 채널을 쓴다(materialFlags bit1 분기 —
baseColor와 같은 규약). 시간(total/delta)은 프레임 값이라 계속 인스턴스로 온다. 브리지
(`ConvertLegacyMaterial`)가 m_flowInfo→논리 값 폴백을 승계하고 역변환이 m_flowInfo를
동기화한다 — 제품 sealing 경로의 픽셀은 그대로다.

실측이 가른 함정 셋: ① **VS가 b2를 읽으면 root signature CBV 스테이지 가시성이 어긋난다**
(PSO 생성 E_INVALIDARG) — 선택은 PS에서 한다(PS는 이미 b2 소비자). ② **reflection이 CB
크기를 패딩 없이 보고한다** — float4를 마지막에 둬야 총합이 16B 정렬로 끝나 pass 검증(%16)을
지난다. 그래서 선언 순서는 float2(uvScroll)→float4(windVector)가 계약이다(48B 표준 프리픽스
뒤, b2 = 무tail 80B / tail 96B). ③ **vk.forward의 수동 packet 경로는 브리지를 우회한다** —
테스트가 flow를 m_flowInfo로만 저작해 기대식이 0.11376 어긋났고, 논리 값 저작을 더해
기대식과 정확히 일치(0.2598≈0.26)로 복귀했다. 검증: dx12 스윕 기준선 일치(28·4·2·1),
dx12.forward/forwardshade·vk.forward/gbuffer/grid(테스트 계약을 80/96B·flow 항목 포함으로
갱신), matmigrate 합성 22(flow 승계·역동기화 — 변이가 정확히 2건을 붉힘), ft-primitives·
material/scene 코퍼스 전수. Inspector 동적 편집기에는 flow가 자동으로 나타난다(meta 선언
기반). 인스턴스 flow 채널의 wind/uvScroll 은퇴는 legacy 경로와 함께 I6의 몫이다.

**S2b 잔여 선결 해소 실측 (2026-08-30) — useNormalMap·IOR은 표현할 것이 없었다.**
정찰이 두 필드 모두 "새 정본이 표현해야 할 저작"이 아님을 증명했다:
- **useNormalMap은 유도 상태다.** `MaterialInfomation::reflect()`에 없어 **애초에 직렬화된
  적이 없고**, `UseTextureMap`이 normalMap 텍스처 존재에서 매번 재구축한다. 범프맵 상태값
  `USE_BUMP_MAP(2)`은 소비자 0이었다 — 셰이더 어디에도 gNormalState/bump 분기가 없고,
  enhanced 경로는 스냅샷 빌드·sealing에서 `!= 0 → 1`로 눌러버리며, draw 검증은 이미
  `useNormalMap <= 1`을 강제한다(EnhancedRenderPass.h). 생산자는 legacy Assimp 로더의
  `aiTextureType_HEIGHT` 폴백 1곳뿐 — **범프맵 경로를 은퇴**시켰다(사용자 지시:
  UseBumpMap·USE_BUMP_MAP·HEIGHT 폴백 제거). 이제 useNormalMap은 순수 0/1 유도값이다.
- **IOR은 소비 0인 죽은 저작이었다.** 유일 소비자 `TrySetMaterialInfo`(gIOR→"PBRMaterial"
  CB)는 **호출자 0인 죽은 함수**였고, PBRMaterial CB는 현존 셰이더 어디에도 없다.
  Inspector 슬라이더는 아무도 읽지 않는 값을 저작하고 있었다 — 슬라이더와 죽은 함수를
  걷었다. 실측 방증: 씬·재질 코퍼스의 m_IOR 저작값이 전부 기본값 1.5로, 잃는 데이터가
  0이다. `m_IOR` 필드·reflect 항목·DataSystem clamp는 씬 포맷 안정을 위해 S2c/I6까지
  유지한다(로드 호환 — 값은 이제 어디에도 흐르지 않는다).

이로써 ★ "S2b 보류 이유"의 세 표현(flow·useNormalMap·IOR)이 전부 해소됐다 — flow는
논리 property 승격으로, 나머지 둘은 유도/은퇴 판정으로. S2b(writer 전환)는 열려 있다.

**M5-S2b 완료 실측 (2026-08-30).** writer 이중화 — 읽기 이중화(S1)의 대칭이다.
`SerializeMaterialPayload`가 ShaderMeta를 아는 재질(m_shaderMetaGuid 유효 + m_cbufferValues
공백)을 `ConvertLegacyMaterial`+`SerializeMaterialAuthoring`으로 새 정본에 적고, meta 부재·
변환/인코딩 실패는 legacy 표기로 폴백한다(폴백은 로그, nil shaderAssetId는 코덱이 fail-closed
거부하므로 검사가 이중이다). 씬·프리팹 embed의 절단선은 `MeshRenderer::OnAfterSerialize` —
reflection shared_ptr 멤버는 컴파일 타임 재귀라(S2a와 같은 제약) 쓰기 쪽도 소비자 훅이
유일한 절단선이고, 기존 H3 훅(OnAfterSerialize)이 이미 emit 경로에 있어 리플렉션 무변경으로
성립했다. standalone `.asset` writer(SaveMaterial/EditorAssetDatabase)도 같은 함수라 함께
전환된다.

★ **눈먼 초록을 하나 잡았다 — 씬 코퍼스는 writer 전환을 원리적으로 못 본다.** 14개 씬의
재질 embed 전수가 m_shaderMetaGuid nil(기본 PBR)이라, canonical 분기가 통째로 죽어도
28/28이 초록이다(실측: 첫 실행에서 canonical 0건·legacy 11건 전부 폴백으로도 안정 통과).
corpus probe의 stable=yes도 형식을 관측하지 않는다. 그래서 matmigrate에 **실자산
(ForwardWater.asset) canonical 관측 단정**을 신설해 이 사각을 게이트에 박았다. 검증:
matmigrate 실사 9→16(writer 정본·meta 논리 값 생존·저장→로드→재저장 고정점·meta 부재
legacy 폴백·컴포넌트 embed 정본·실자산 canonical), 변이(canonical 분기 차단)가 정확히
writer 단정 3건만 붉힘(고정점·폴백은 legacy에서도 성립해야 하므로 초록 — 예측 일치).
씬 코퍼스 28/28·재질 코퍼스 2/2·프리팹 3종(corpus/roundtrip/duplicate)·리플렉션 골든
diff 0(골든 재질은 nil meta → legacy 폴백이라 형상 무변경)·ft-primitives 실드로우·
matseal/matparity/matscript·dx12/vk 픽셀 게이트 전부 초록. 잔여: 현 씬 코퍼스에는
shadermeta 재질 embed가 0이라 전환의 실효는 앞으로의 저작부터다. 피커 열거
(SnapshotMaterials)는 legacy 유지(S2c/I6).

**S2c-2 착수 정찰 (2026-08-30) — 소유 분리는 저작과 런타임이 다른 수술이다.**
`m_Material` 제품 소비자 전수(테스트 제외 8파일): ① **프록시 사슬** — MeshRenderer →
ProxyCommand payload(ProxyCommand.h:62) → PrimitiveRenderProxy::m_Material(117) →
EnhancedSceneRenderer pooled.materialSource → BuildSealSourceFromLegacy. legacy
`shared_ptr<Material>` 핸들이 렌더 스레드 경계를 통째로 건넌다 — 런타임 소유 전환은 이
사슬의 타입 전환과 동시일 수밖에 없다(빅뱅 위험). ② **required-asset packet** —
SceneManager::CaptureRequiredRenderMaterials가 renderer·FoliageType에서 shared_ptr를 수집
(App/PlayerApp 소비). ③ **Foliage** — FoliageType.m_material은 비직렬화(OnDeserialized가
모델에서 재해석)라 분리 가능한 저위험 조각. ④ **Inspector/CLR** — S3/S4로 값 경로는 논리
값이 됐고 남은 것은 핸들 노출(이름 버튼·renderingMode enum·피커 undo·CLR raw ptr).
⑤ 캐시 표면(DataSystem::Materials/Snapshot/Insert/Duplicate)은 I6 은퇴 대상.

경제적 절단: **저작 소유(2a)** 는 S2a/S2b 훅이 이미 m_Material 노드의 읽기/쓰기를 전담하므로
reflect에서 m_Material을 빼고(스키마의 잉여 키는 무시되므로 old scene 호환) base 자산
GUID+override 저작 표기를 훅에 더하는 점진 수술이 가능하다 — 지금 피커는 공유 자산 재질을
고르면 저장 시 전체를 인라인 embed해 **자산 연결이 저장에서 소실**되는데, 2a가 이것을 참조
의미론으로 고친다(experiment::MaterialInstance의 override 표현을 저작 스키마로 그대로 씀).
**런타임 소유(2b)** 는 MaterialInstance(base가 experiment::Material인 비영속 타입)를
MeshRenderer에 들이고 프록시에 effective 스냅샷을 흘려 sealing의 legacy 브리지를 우회하는
큰 수술 — I5-D(experiment::Model 직접 소비)와 결이 같아 그 옆에서 함께 판정하는 것이
맞다. 주의: 훅(OnDeserialized/OnAfterSerialize)은 PHASE 17 D3-a가 시그니처를 이행 중인
표면이라 2a 착수 시 조율이 필요하다.

**M5-S2c-2a 완료 실측 (2026-08-30).** 저작 소유 분리 — base 자산에 링크된 재질은 인라인
embed 대신 **base 참조+인스턴스 diff**를 적는다(`m_Material: {ref, blendMode?,
keywordSelections?, overrides?}` — override 값은 S0 코덱의 타입 키 표기를 공개 창구
`Serialize/DeserializeMaterialPropertyValue`로 재사용, 두 번째 표기 금지). 쓰기 diff는 현
재질과 base를 같은 meta로 experiment 변환해 항목별 인코딩 텍스트로 비교한다(수학 타입에
operator==가 없다). base에만 있는 저작은 참조 표기가 "되돌림"을 표현할 수 없어 인라인
폴백(fail-open, 로그). 읽기는 base 소유 사본에 `ApplyPropertyToLegacy`(ConvertToLegacy와 값
변환 정본 공유 — `ConvertPropertyToLegacyValue`·`SynchronizeLegacyScalarMirrors` 추출)로
override를 겹친다. 피커는 자산 링크의 생산자다 — 공유 캐시 객체를 그대로 물던 결함(인스턴스
편집의 전파 + 저장 시 자산 연결 소실)을 소유 사본+`m_materialBaseGuid`로 교정했고, 링크는
GUID가 실제 standalone 재질 자산으로 해석될 때만 건다(모델 내장 재질의 m_fileGuid는 모델
GUID다). Instantiate(Inspector·CLR)는 링크 해제. S2c-1의 모델 폴백은 base 링크 재질에서
차단했다(재질 자산 GUID를 모델 GUID로 오인 금지).

★ **m_Material의 reflect 퇴출은 2b로 미뤘다** — 프리팹 패치 경로(PrefabUtility의
`Meta::Deserialize`)는 postLoad 없이 typed 읽기만 하므로, 지금 빼면 프리팹 재질 오버라이드가
조용히 소실된다. typed는 ref 노드에서 기본값 재질을 만들고 postLoad가 교체한다(프리팹
오버라이드에 ref 표기가 실리는 경우는 아직 없다 — 2b에서 패치 경로와 함께 판정).
검증: matmigrate 실사 26(+7: ref 저장·diff 최소성·로드 실체화·링크 복원·재저장 고정점·
무편집 diff 0·base 로드), override 적용 변이가 정확히 2건(로드 실체화·고정점)만 붉힘 —
예측 일치. matseal 20(변환기 refactor 무변화 증명)·matparity·matscript·matcodec, 씬 코퍼스
28/28·프리팹 3종·리플렉션 골든 diff 0·ft-primitives·dx12/vk 픽셀 초록.

**M5-S2c-1 완료 실측 (2026-08-30).** 모델 GUID 자립 — S2c(소유 분리)의 선행 절단.
`MeshRenderer::m_modelGuid`(reflect 말미 추가)가 메시 출처 모델의 정본 주소가 된다. 예전에는
인라인 재질의 `m_fileGuid`가 모델 GUID를 나르는 편법이었다(재질 것처럼 보이지만 모델
주소 — SceneCookProducer 실측 주석). 배선 전수: ① OnDeserialized가 m_modelGuid 우선 해석,
legacy 씬은 편법 폴백 후 **읽는 즉시 이주**(로드 실패여도 정보 보존 — 다음 저장부터 정본).
② 생성 경로 ModelSceneBridge 4곳이 model->guid를 직접 배선. ③ SceneCookProducer가
m_modelGuid를 model edge로 계수(이주기 씬은 카운터 중복 가능하나 dependencies는 dedupe라
폐포 정확). ④ **InstantiateOwned m_fileGuid 비승계** — S3의 족쇄가 풀렸다. 클론에 자산
GUID가 남으면 씬 embed의 assetId가 원본을 사칭한다. 검증: matmigrate 실사 19(TypeOps
postLoad 창구로 이주 검증 — 훅 시그니처가 D3-a 이행 중이라 안정 디스패치를 씀), 이주 줄
변이가 정확히 1건 붉음, matscript 비승계 단정 전환(구 승계 단정이 곧 RED 증거), 리플렉션
골든 diff 정확히 1줄(m_modelGuid) 검산 후 -Baseline 재생성, 씬 코퍼스 28/28 + **저장본
실물 검증**(FT_Primitives pass1의 renderer 8개 전부 non-nil m_modelGuid 이주 확인 — 눈먼
초록 방지), experiment.scenecook·ft-primitives·프리팹 코퍼스 초록. 잔여: 신규 canonical
embed(assetId ≠ 모델)가 늘면 legacy 편법 키는 자연 소멸 — cook 폐포의 m_fileGuid 계수는
I6에서 은퇴.

★ **I5-M1 이 첫 슬라이스인 이유는 소비자 때문이다.** 새 타입만 만들면 "생산만 있고 소비 0" 이
된다 — 이 저장소가 반복해서 밟은 형태다. I5-M1 은 legacy 가 만드는 CB bytes 와 **비트 단위로
대조**하므로 게이트가 곧 의미 있는 소비자이고, 그 패리티가 서야 I5-M4 의 sealing 치환이 안전하다.
패리티가 깨지면 그 자리가 `experiment::Material` 의 실제 격차다. 정본 packer 를 양쪽이 공유해도
게이트는 무의미해지지 않는다 — 게이트가 재는 것은 알고리즘이 아니라 **입력 데이터 모델**,
곧 `experiment::Material` 의 property·keyword 가 같은 바이트를 재현할 만큼 충분한가다.

**I5-M 완료 게이트:**

- [ ] D5/I5 resolver가 D2 identity를 받아 모든 `experiment::Material.assetId`·`shaderAssetId`와
      `TextureReference.assetId`를 채우고 nil/충돌을 게시 전에 거부한다.
- [ ] GBuffer/Forward/Water/Wind/Foliage 제품 sealing이 `experiment::Material` 또는
      `MaterialInstance`에서 기존 M6 snapshot을 만들며 `::Material`을 읽지 않는다.
- [ ] MeshRenderer·Foliage·Scene serialization·standalone material asset·Editor
      picker/Inspector·CLR property API가 새 definition/instance 경계를 소비한다.
- [ ] CLR의 인스턴스 변경은 논리 property override를 갱신하며 runtime CB 이름이나
      `Material::RuntimeSchema` 설치 여부에 기대지 않는다.
- [ ] legacy `.asset`은 별도 migration DTO/codec이 `experiment::Material`로 한 번
      정규화하고, 제품 writer는 새 정본만 기록한다.
- [ ] 양 backend에서 Standard/transparent/Water/Wind/Foliage와 ShaderMeta/texture
      generation reload, instance 독립 override, save-load-resave parity를 통과한다.
- [ ] 제품 `experiment::Model`/`experiment::Material` 외부 소비자가 0이 아니며,
      legacy `::Material` 제품 소비자는 migration/parity 경계를 제외하고 0이다.
- [ ] CB packing 정본이 하나다 — `Material.cpp` 안에 packing 구현이 남아 있지 않고
      legacy 두 호출부와 `experiment::Material`이 같은 `MaterialPropertyPacker`를 부른다.
- [ ] I5-M1~M5·I5-D의 어떤 커밋도 `Engine/RenderEngine/Material.cpp` 본체를 편집하지 않는다
      (소비자 편집과 I6 삭제만 허용).
- [ ] legacy 본체의 experiment 명목 공개 표면이 더 늘지 않는다 — `Model.h` 패리티 getter
      3종(`72ed27ef`)이 마지막 사례이며 I6에서 legacy `::Model` 본체와 함께 은퇴한다.

### I6. 전환 — Assimp 은퇴

I5 결정 후. 두 경로 병행 기간을 두고 픽셀·성능 대조를 거친 뒤 legacy 로더를
제거한다. **Release 로만 성능을 판정한다**(Debug 는 같은 조건에서 25배 느리고
규모별 개선 방향까지 뒤집는다).

I6는 Assimp만 떼는 단계가 아니다. I5-M parity가 닫히면 `ExperimentLegacyBridge`,
legacy `::Model`/`::Material`, `DataSystem::Materials`, `Material::InstantiateShared`,
legacy model/material runtime codec과 프로젝트 등록을 함께 제거한다. 과거 자산 지원이
필요하면 제품 타입을 존치하지 않고 오프라인 migration reader만 별도 도구 경계에 둔다.
완료 판정은 legacy Model/Material/Assimp 제품 호출 0, 대표 자산 픽셀 동등, Release
성능 비퇴행, VS18/v145의 RenderEngine→SceneRuntime→RenderTests→CreatorEditor→Player
빌드와 DX12/Vulkan 회귀 통과다.

### I7. cooked 경로 — **포맷·코덱 완료** (2026-08-25 · `c8e06ffa`)

`Experiment/Cooked/CookedModelFormat.h` · `CookedModelCodec.cpp` 신설.
SerializationPlan(PHASE 17) 의 런타임 쿠킹과 같은 결정을 공유한다.

★ **구 V0 의 요구가 여기서 전부 이행됐다.** 포맷을 신설하면서 처음부터 넣었다:

| 필드 | 실제 |
|---|---|
| 매직 | `kMagic = 0x434D4543` — legacy `.asset` 은 첫 4바이트가 바로 `nodeCount` 라 구버전 판별 수단이 없었다 |
| 포맷 버전 | `kFormatVersion = 3` — v2의 bounds 의미 변경에 이어 v3에서 고정 `Vertex[]`를 mesh별 packed byte 범위로 바꿨다 |
| 레이아웃 해시 | `FileHeader::vertexLayoutTableHash` — 전체 표에서 유도. 불일치면 거부하고 재임포트 |
| 헤더 요약 | `maxVertexStride` · `vertexAttributeMaskUnion` — 모든 mesh 레코드와 교차 검증 |
| mesh별 배치 | `vertexByteBegin` · `vertexCount` · `vertexStride` · `vertexAttributeMask` — 같은 표에서 범위·stride를 유도·검사 |

단정도 이빨 있는 형태로 들어갔다 — 단순 상수 비교가 아니라
**표에 적힌 크기의 합**과 비교한다. 필드를 더하면서 상수만 고치는 것으로는 통과할
수 없다(트랙 V1 의 `ValueStreams` 단정과 같은 논리).

★ **V3 전환까지 완료했다.** cooked 포맷은 별도 필드 표나 별도 FNV 구현을 소유하지
않는다. `VertexLayoutTableHash()`와 mesh별 `StrideOf(mask)`가 같은 V1 표를 사용한다.
table hash/header union/header max stride/mesh mask/mesh stride/byte range 중 하나라도
다르면 읽기를 거부한다. 디코드는 임시 `ModelDraft`에 수행해 늦은 손상 검출도 호출자
출력을 반쯤 채우지 않는다.

남은 것: 디코더를 `ModelSourcePreference` 경로에 배선하는 일(`CookedOnly` ·
`CookedThenSource` 가 실제로 쓰이게).

★★ **경로 규약은 여기서 발명하지 않는다** (2026-08-25 결정).

I7 의 범위는 **포맷과 속도뿐**이다. 산출물이 *어디에* 놓이는지는 정하지 않는다.

왜: 쿠킹 산출물의 주소는 결국 **GUID** 여야 하는데, 그 GUID 채번이
SerializationPlan §3.4 에서 바뀔 예정이다(파일명 해시 → 랜덤 UUIDv4 + git 추적).
지금 경로를 설계하면 채번이 바뀔 때 두 번 고치게 되고, 더 나쁘게는 **현재의
파일명 충돌이 캐시 충돌로 옮겨간 뒤 원인이 안 보이게 된다**.

실측(2026-08-25): 프로젝트 자산 275개 중 stem 충돌 17건. 대부분은 원본+쿠킹 쌍
이지만 `AnimatorController/MonsterC.json` 과 `NodeEditor/MonsterC.json` 은 **서로
다른 자산인데 같은 GUID** 다(`MakeFileGUID` 가 파일명만 해시하므로).
`AssetMetaRegistry::Register` 는 충돌 검사 없이 후입이 선입을 덮는다.

따라서 I7 이 하는 일은 이것뿐이다:

- 쿠킹 경로를 만드는 지점을 **헬퍼 하나로 모은다**(현재 legacy 는 쓰기·읽기·판정이
  세 군데로 갈라져 있고 그중 판정만 다른 곳을 본다 — 아래 함정 참조).
- 그 헬퍼가 나중에 `CacheRoot` + GUID 로 바뀌면 이관이 끝나게 둔다.

★ legacy 가 이미 이 함정을 밟았다 — 그대로 물려받지 않는다:

| | 경로 |
|---|---|
| 쿠킹 **쓰기** (`RequestModelCacheWrite`) | `PathFinder::Relative("Models\\") / <stem>.asset` [고정] |
| 쿠킹 **읽기** (`LoadModelFromAsset`) | 같음 [고정] |
| 쿠킹 **사용 판정** (`Model::LoadModel`) | `<원본과 같은 폴더>/<stem>.asset` |

`Assets/Models/` 밖의 모델은 쿠킹이 있어도 판정에서 탈락한다. 실측:
`Animation/Ani_Mon_3_die.fbx` 는 `Models/Ani_Mon_3_die.asset`(1.01MB)이 있는데도
매번 Assimp 68.9ms 를 돈다.

### ★ I7 의 목표 수치 — 실측으로 확정 (2026-08-25)

`ModelLoader::CookedLoadBreakdown` 계측 결과(Release, 반복 20):

| 자산 | 쿠킹 총 | **스켈레톤** | 노드 | 메시 | 재질 | 텍스처(공유) |
|---|---|---|---|---|---|---|
| Gunner_F_Mythic | 6.257ms | **4.981 (79.6%)** | 0.037 | 0.834 | 0.330 | **0.012** |
| SU_Mythic | 7.107ms | **5.779 (81.3%)** | 0.048 | 0.929 | 0.294 | **0.011** |
| Prim_Suzanne(스킨 없음) | 0.311ms | 0.000 | 0.002 | 0.153 | 0.090 | 0.003 |

**텍스처는 0.012ms — 임포터가 건드릴 수 있는 몫이 99.8% 다.** 비교에서 빼야 할
공유 비용이 사실상 없다. (착수 전 나는 이 미설명 구간을 "재질·텍스처가 유력"
이라고 적었고 틀렸다. 스켈레톤이었다.)

원인은 **키의 표현이 디스크와 메모리에서 다르다**는 것이다 — 디스크는
`XMFLOAT4 + double`, 메모리는 `XMVECTOR + double`. 그래서 일괄 복사가 원리적으로
불가능하고 키마다 `read()` 두 번이 나간다. Gunner 기준 `read()` 70,647회 중
68,508회(97%)가 키프레임인데 바이트로는 43% 다. 스킨 없는 Suzanne 이 `read()`
22회로 끝나는 것이 같은 얘기의 뒷면이다.

절감 내역(파일 밖 마이크로 벤치, 같은 자산·같은 바이트):

| 수단 | 절감 | 누구 것인가 |
|---|---|---|
| 슬럽(파일 한 번 읽고 포인터 전진) | **3.29ms** | legacy 도 할 수 있다 |
| POD 키(blittable) + 인덱스 뼈(map 제거) | ~1.5ms | **experiment 고유** |

목표: **6.3ms → ~1.0ms**.

★ **mmap 은 쓰지 않는다.** 실측에서 `ReadFile` 보다 느렸다 — 페이지 폴트가
memcpy 보다 비싸다. 따라서 zero-copy·문자열 테이블·타입 변경도 근거가 없다.

| 방법 | Gunner 1.85MB |
|---|---|
| `ReadFile` 1회 | **0.235ms** |
| `MapViewOfFile` + 전 페이지 터치 | 0.671ms |
| `ifstream` 1회 | 1.210ms |
| `ifstream` legacy 패턴(70,647회) | 3.52ms |


### ★ I7 이행 결과 (2026-08-25)

난 것: `experiment::cooked` 포맷 + 코덱 + 게이트.
안 한 것: **경로 규약**(SerializationPlan §3.6.1 소관, §3.4 선행).

#### 수치 (Release, 반복 20, 쿠킹 대 쿠킹)

| 자산 | legacy `.asset` | experiment `.cemc` | |
|---|---|---|---|
| Gunner_F_Mythic | 4.883ms | **1.708ms** | 2.86배 |
| SU_Mythic | 5.507ms | **1.849ms** | 2.98배 |
| Prim_Suzanne | 0.515ms | **0.113ms** | 4.58배 |

목표는 ~1.0ms 였고 1.708ms 에 닿았다. **차이의 정체는 검증이다** —
예측을 적을 때 `ModelLoader::Validate`(0.460ms)를 아예 빼먹었다. legacy 는
검증을 하지 않으므로 이건 experiment 가 **더 하는 일**이고, 그러고도 이긴다.

#### 구간 (각각의 min — 서로 더할 수 없다)

Gunner 기준: 파일 읽기 0.159 · **파싱 0.276** · 검증 0.460 / 전체 1.708.

파싱이 0.276ms 다 — legacy 가 같은 일에 쓰던 스켈레톤 4.981ms 의 1/18 이다.
포맷의 목표는 달성됐다.

#### ★ 반성 — 포맷을 잘 만들어도 읽는 수단이 병목이면 소용이 없다

처음엔 `std::ifstream` 으로 짜서 2.827ms 였고, 그중 **1.762ms 가 파일
읽기**였다(파싱은 0.318ms 뿐). 수단을 바꿀 근거를 물어본 실측:

| 수단 (Gunner 1.85MB, 30회 min) | |
|---|---|
| `ifstream` 1회 read | 1.123ms |
| **`fopen`/`fread`** | **0.159ms** ← 7.1배 |
| `ReadFile` 1회 | 0.161ms (같다 — Windows API 를 쓸 이유가 없다) |
| `MapViewOfFile` + 전 페이지 터치 | 0.600ms |

그래서 디코더는 `fread` 를 쓴다. **mmap 은 안 쓴다** — 이미 진 자다.

부수적으로 벌어진 교훈 하나: 디코더를 fread 로 바꿄 뒤에도 벤치의 구간
프로브가 `ifstream` 을 계속 재서 1.76ms 를 찍었다 — **하지도 않는 일을 재는
계측기**였다. 프로브는 구현과 같은 수단을 써야 한다.

#### 게이트 — `experiment.cooked`

합성 223건 + 실자산 4,902건. **첫 실행부터 전부 초록이었으므로 변이로
이빨을 증명했다**(§3.3 규칙):

| 변이 | 합성 223 | 실자산 4,902 |
|---|---|---|
| 정점 레이아웃 해시 검사 제거 | **3건 실패**(해당 항목만) | 0 |
| 키 시작 오프셋 무시 | **0 ← 구멍** | **616건 실패** |
| 파일 크기 검사 제거 | **3건 실패** | 0 |

★ 두 번째 변이가 합성 검사의 구멍을 드러냈다. 합성 draft 의 모든 채널이
`translationBegin == 0` 이라 범위 계산 버그가 원리적으로 보이지 않았다.
**합성 검사의 존재 이유가 "실자산이 안 밟는 형태"인데 반대가 됐다.**
범위가 서로 다른 채널 셋으로 보강해 이제 합성도 2건을 잡는다.

#### 포맷 요점

가변 개수를 갖는 것은 전부 **연속 POD 블록 + [begin, count) 범위**다.
키 세 종은 디스크와 메모리 표현이 같아서 `assign` 한 줄로 끝난다 —
legacy 가 시간의 80% 를 쓰던 바로 그 자리다.

헤더가 들고 있는 것: 매직 · 포맷 버전 · **유도된 전체 정점 레이아웃 표 해시** ·
mask union · max stride · 파일 크기. 각 mesh는 packed vertex byte 범위·count·mask·
stride를 들고, 모두 V1 표에서 대조한다. V2의 단일 68B mask에서 V3의 mesh별 배치로
확장했어도 레이아웃 정본은 늘지 않았다.

#### 남은 것

- 경로 규약(SerializationPlan §3.4 → §3.6.1). 그것이 오기 전까지 쿠킹
  산출물은 호출자가 `ModelLoadRequest::cookedPath` 로 들고 온다.
- `CookedThenSource` 선택과 신선도 판정 — 그것도 경로 결정을 기다린다.
- 검증 0.460ms 를 쿠킹 경로에서 줄일것인가. 굽는 시점에 이미 검증했으므로
  중복이긴 하나, **손상된 캠시를 신뢰하는 대가**가 얼마인지 먼저 재야 한다.

### I8. 검사 자산·게이트 보강

- 값이 변하는 Step 트랙을 가진 자산 확보(또는 합성 자산 생성)
- 실자산 게이트에 "코너 탄젠트가 자기 삼각형 UV 기울기와 부호가 맞는가" 추가
  — **정상 코드에서 기준값을 먼저 재고 임계를 정한다.** 평활화된 이음매에서
  거짓 실패가 날 수 있다.
- `Cha_Mon_5.fbx` 처럼 기준선 없는 자산의 판정 방식

---

## 4-V. 이행 — 트랙 V (정점 레이아웃)

트랙 I 와 **병행 가능**하다. 의존은 아래 각 항에 적는다. 실측 근거는 §1.7.

원칙 하나로 줄이면:

> **"어떤 속성이 있는가"의 정본은 IR 의 `VertexStreams` 하나이고, 나머지는 전부
> 거기서 유도된다.**

이건 새 설계가 아니라 이미 헤더에 적힌 규약이다(`ImportedScene.h:90` — *"속성
없음"을 센티널 값이 아니라 빈 스트림으로 표현*). 지금은 그 규약이 변환 경계에서
96B 고정 AoS 로 떨어지며 무너진다.

### ★ 작업 대상은 experiment 계층이다

**legacy 는 건드리지 않는다.** `::Vertex`·`Mesh.h`·legacy `ModelLoader` 는 I6 에서
통째로 은퇴하므로 투자하지 않는다. 트랙 V 가 바꾸는 것은 `experiment::Vertex` 와
그 생산 경로뿐이다.

실측이 이 경계를 지지한다.

| | legacy | experiment |
|---|---|---|
| 캐시·직렬화 | `.asset` 있음 (버전 필드 없음) | `.cemc` v3 있음 — 표 hash + mesh별 mask/stride를 검증 |
| `Vertex` 소비자 | 입력 레이아웃 5 · 셰이더 4 · 캐시 | 자기 파이프라인(생산·검증) + 검사 하네스 5파일 |
| 이 트랙의 처분 | I6 은퇴 | **정본으로 승격** |

★ legacy `.asset`은 legacy `::Vertex`로 읽고 쓰므로 experiment를 바꾸는 것과
무관하다. experiment `.cemc`는 포맷 버전과 유도된 표 hash/mask/stride 불일치로
구버전을 거부하므로 레이아웃 변경을 조용히 오독하지 않는다.

렌더 경로(입력 레이아웃·셰이더)가 새 레이아웃으로 옮겨 가는 시점은 **I5 치환**이다.
그전까지 트랙 V 의 변경은 화면에 아무 영향이 없다 — 그래서 지금이 싸다.

### V0. 레이아웃 버전 규약 — **생산자는 V1, 소비자는 I7**

착수 시점에는 "legacy `.asset` 에 버전 필드가 없어 레이아웃 변경이 기존 캐시를
조용히 오독한다"를 근거로 독립 슬라이스로 뒀다. **범위가 두 군데 틀렸다.**

1. **legacy 캐시는 트랙 V 의 대상이 아니다.** `::Vertex` 를 건드리지 않으므로 깨지지
   않고, 그 코드는 I6 에서 은퇴한다 — 곧 버릴 곳에 버전 필드를 넣을 뻔했다.
2. **experiment 캐시는 "없는" 것이 아니라 "아직 없는" 것이다.** I7(cooked 경로)에서
   신설된다. 요구는 살아 있다.

바로잡은 분업은 이렇다. **레이아웃 버전은 레이아웃의 속성이지 캐시의 속성이 아니다** —
캐시 말고도 그것을 소비할 곳(툴 간 교환·게이트 단정)이 생긴다.

| 역할 | 담당 |
|---|---|
| 레이아웃 버전·해시를 **만든다** | **V1**(기술표에서 유도) |
| 메시별 속성 마스크를 **정한다** | **V3** |
| 캐시 헤더에 **기록하고 검사한다** | **I7** |

따라서 이 항목은 독립 슬라이스가 아니라 V1·V3·I7 에 나뉘어 산다. 의존 방향도
뒤집힌다 — **I7 이 트랙 V 를 기다린다**(마스크가 정해져야 기록할 것이 정해진다).

### V1. 속성 기술표 — 정본 하나 · **V 트랙의 착수점**

속성마다 `{ 이름, 시맨틱, 포맷, 크기, 퍼뮤테이션 축 }` 을 한 곳에 둔다. 오프셋은
마스크에서 계산되므로 사람이 세지 않는다.

**진행 상태 — 코드·검증 완료 (2026-08-25)**

- [x] **재용접 스트림 순회** (`22392518`) — `VertexStreams::ValueStreams()` 를 목록
      정본으로 세우고 `NormalGeneration`·`TangentGeneration` 이 순회하게 했다.
      단정을 크기 상수가 아니라 **목록 원소 수**와 비교하도록 짜서, 필드를 더하며
      상수만 고치는 것으로는 통과할 수 없게 했다(변이 양방향 증명).
      무회귀: 게이트 6종 지표 26줄 차이 0.
- [x] **속성 기술표 신설** — `Experiment/VertexLayout.h`.
      `{속성·이름·시맨틱·시맨틱 인덱스·포맷·퍼뮤테이션 축}` 과 마스크 연산
      (`StrideOf`/`OffsetOf`/`VertexLayoutHash`). **오프셋은 마스크에서 계산되므로
      사람이 세지 않는다.**
- [x] `streams.colors` 소실(§1.7) 닫기 — FBX와 glTF `COLOR_0`를 변환 경계의
      optional color stream으로 보존하고 `SU_Mythic` 실자산으로 확인했다.
- [x] **레이아웃 버전을 표에서 유도**(구 V0) — `VertexLayoutHash(mask)`.
      손으로 올리는 숫자가 아니라 시맨틱·포맷·오프셋에서 유도되므로 속성을 더하거나
      포맷을 바꾸면 자동으로 달라진다. I7 이 이미 같은 형태를 캐시 헤더에
      갖고 있다(`FileHeader::vertexLayoutTableHash` — 불일치면 거부하고 재임포트).
- [x] **대조 단정으로 표가 현실을 기술함을 증명** — 검사 계층에 표의
      `StrideOf`/`OffsetOf` 대 legacy `::Vertex` 의 `offsetof` 11종.
      **변이로 이빨을 확인했다**: 표에서 uv0 를 8B→12B 로 밀면 uv1 이후 다섯
      단정이 정확히 실패하고 position·normal·uv0 는 통과한다 — 안 밀린 것은
      안 잡는다.
- [x] **게이트 12종 전수 통과** — tangent·normal·sampler·cooked·import·gltf·fbx·
      model·anim·bench·weld·cacheopt. V3 최종 변경 뒤 `SU_Mythic` glTF와 cooked를
      다시 실행해 color 1/layout 위반 0, 합성 280/280·실자산 6,217/6,217을 확인했다.
      ★ 게이트를 인자 없이 부르면 "사용법:" 만 찍고 **종료 코드 0** 을 낸다 —
      절대 경로를 주지 않으면 아무것도 검증하지 않은 채 통과처럼 보인다.
- [x] **cooked 표를 이 표에서 유도하도록 전환** — V2에서 중복 표/FNV 구현을
      제거하고 `VertexLayoutHash(kV2VertexAttributes)` 하나로 통합했다.

★ **입력 레이아웃 5곳의 오프셋 대체는 V1 이 아니라 V4 다.** 그 5곳은 legacy
`::Vertex` 를 전제하고, 트랙 V 는 legacy 를 건드리지 않는다(§4-V 서문). 렌더 경로가
표를 쓰기 시작하는 시점은 **I5 치환**이다.

V1 당시에는 인터리브 스킨 때문에 legacy `::Vertex`만 표와 대조할 수 있었다.
V2에서 검증 대상을 `experiment::Vertex`로 옮겨 offset 0·12·24·32·48·52와 stride
68을 헤더 자체에서 직접 단정했다. `ExperimentLegacyBridge`는 이제 두 표현의 크기가
다름과 필드 변환 파리티만 검사한다.

의존: **없음.** V 트랙의 첫 슬라이스다.

### V2. 무손실 68B — ✅ 구현·검증 완료 (2026-08-27)

셋 다 실측으로 무손실이 확인됐다(§1.7).

- `uv1` 제거 — 전수 `uv1 == uv0`
- `bitangent` → `tangent.w` 부호 — 셰이더 재현 대조 불일치 0
- `boneIndices` float4 → `uint8×4` — 실측 최댓값 60
- ★ **스킨 배치를 인터리브에서 분리로 되돌린다**(§1.8). 이것은 절감이 아니라
  **정확성 수정**이다 — 지금 배치는 GPU 입력 레이아웃으로 기술할 수 없다.

초안의 64B는 산술 오류였다. 코어 48B + `uint8×4` 4B + `float4` weight 16B는
**68B**다. 정점 21.42MB → 15.17MB(29.2%). 64B를 만들기 위한 weight 압축은 별도
오차·셰이더 계약이므로 무손실 범위에서 제외했다.

구현은 experiment `Vertex`·변환·loader 검증·legacy bridge·cooked 포맷·검사를 함께
움직였다. cooked는 V1 표에서 mask/hash/stride를 유도하고 구 mask 입력을 거부한다.
legacy 입력 레이아웃 5곳과 셰이더 `VSIn` 4곳은 여전히 legacy `::Vertex`를 소비하므로
건드리지 않았다. 그 생산 소비자 전환은 I5/V4에서 같은 레이아웃 마스크로 묶는다.

**검증:** Debug x64 `RenderTests`·`CreatorEditor` 빌드, 12개 experiment 게이트 전수
통과. `experiment.bench Gunner 1`은 96B → 68B, 정점 바이트 0.86MB → 0.63MB를
확인했다. 화면 소비 전이라 픽셀 차이 0 판정은 I5/V4 게이트로 남긴다.

★ **양자화는 하지 않는다.** uv0 half2 는 `scene.asset` 에서 23텍셀 오차다.

의존: V1.

### V3. 메시별 마스크 — ✅ packed 스트림 구현·검증 완료 (2026-08-27)

고정 `std::vector<Vertex>`를 `VertexBuffer`로 바꿔 각 mesh가 자기 mask·stride·packed
bytes를 소유하게 했다.

- 코어 48B(position·normal·tangent4·uv0) — 모든 메시
- 스킨 20B(`uint8×4` + `float4`) — 스킨 메시에만
- uv1 / color — 그 자산이 실제로 가졌을 때만

`SceneToModelDraft`가 source stream과 실제 skin 채택 여부로 mask를 정하고,
`ModelLoader`는 mask/stride와 optional 값의 유한성을 검증한다. cooked v3는 vertex를
byte blob으로 쓰고 mesh별 범위를 왕복한다. `COLOR_0` VEC3는 alpha 1, VEC4는 alpha까지
보존하며 `COLOR_1`은 명시적 미지원 경고다.

**검증:** VS18/v145 Debug x64 `CreatorEditor` 빌드 성공. 12개 experiment 게이트를
통과했고, V3 최종 변경의 직접 영향 게이트를 다시 실행했다. `SU_Mythic.glb`는
`color 1 · layout 위반 0`, cooked 합성 **280/280**·실자산 **6,217/6,217**이다.
`Prim_Suzanne.glb`는 skin 0/1, 정점 바이트 0.10MB → 0.05MB로 실제 48B 저장을
확인했다. Gunner/FBX 샘플은 모두 skinned라 각각 68B이며 이 자산들만으로는 V3 추가
절감이 없다.

정점 15.17MB → 11.17MB(누적 47.8%)와 V3 증분 약 3.99MB는 기존 52 mesh·233,910정점
감사에 적용한 계산값이다. 전체 자산 재수집 실측으로 과장하지 않는다.

의존: V2.

### V4. 패스의 레이아웃 유도

**수행 시점: I5-D에 묶는다** (2026-08-29 정정 — §4-I I5 슬라이스 분해 ★ 참조).
독립 슬라이스가 아니다.

패스가 `RHIInputElement` 배열을 손으로 짜는 대신 **"내가 필요한 속성"만 선언**하고,
실제 배열은 `(메시 마스크 ∩ 패스 요구)` 에서 생성한다.

- PSO 캐시는 이미 `inputElements` 를 **내용으로 해싱**하므로 그대로 동작한다
  (`DX12PSOManager.cpp:88`)
- 셰이더 `VSIn` 은 **같은 마스크에서 나온 퍼뮤테이션 축**으로 갈린다.
  선례가 있다 — `EnhancedShadowPass.cpp:33` 의 `SHADOW_SKINNING`
- 입력 레이아웃과 `VSIn` 이 같은 출처에서 나오지 않으면 한쪽만 바뀌어 셰이더가
  0을 읽거나 PSO 생성이 실패한다 — **짝 규칙을 검사로 못박는다**

의존: V3. **`ScriptableRenderPipelinePlan` 의 Asset-first Pass 계약이 이것을
전제로 한다** — 경계는 §0 참조.

### V5. 멀티 슬롯 정점 버퍼 — **보류**

진짜 SoA(깊이 프리패스·섀도가 position 스트림만 읽는 것)를 하려면 필요하다.
지금은 인코더가 슬롯 하나만 바인딩한다.

```
m_commandList->IASetVertexBuffers(0, 1, &view);   // DX12Encoder.cpp:159
```

`RHIInputElement::inputSlot` 필드는 있지만 전부 0이고, `RHIMeshBinding` 도
`vertexStride` 하나만 든다.

**보류 사유:** V3 까지만 해도 실측 47.8% 절감이 나온다. 슬롯 분리는 섀도
캐스케이드 대역폭이 실제로 병목으로 잡힌 뒤에 판단한다 — 재기 전에 열지 않는다.

### V6. UV1 스트림 — 트랙 L 연동

**이 항목은 `LightmapBakerPlan.md`(트랙 L)로 이관됐다.** 착수 시점에는 "라이트맵
존치/폐기 결정"이었으나, 삭제된 베이커 구현이 발견되면서(§1.9) 폐기 선택지가
사라졌다 — 라이트맵은 현재 GI 구성이 못 메우는 화면 밖 간접광의 가장 싼 답이다.

여기 남는 것은 스트림 쪽 계약뿐이다.

- UV1 은 **라이트맵 대상 메시에만** 붙는 옵셔널 스트림이다
- 언랩(xatlas)이 정점을 쪼개므로 **스트림은 언랩 단계에서 생성**된다 — 기존 uv1
  슬롯을 재사용하지 않는다
- 트랙 L 의 L1(언랩)이 이 스트림의 유일한 생산자다

의존: V3(옵셔널 스트림). 소비자: `LightmapBakerPlan` L1.

---

## 5. 완료 기준

### 트랙 I

- [ ] 생산 경로가 `IAssetImporter` 를 탄다 — legacy 로더 호출 지점 0
- [x] 대표 glTF·FBX·animation 게이트가 **실물 `ImporterModelDecoder`**를 타고 돈다
- [ ] `ExperimentImportPathSelfTest`의 하네스 전용 `LegacyBridgeDecoder`를 은퇴한다
- [ ] `ImportOptions` 플래그 중 소비자 0 인 것이 `buildMeshlets` 뿐
- [ ] 임베디드 텍스처가 자산으로 해석된다 — property 생략 0건
- [ ] 대표 자산 N종에서 legacy 대비 픽셀 대조 통과
- [ ] Release 성능이 legacy 대비 회귀 없음
- [ ] 합성 검사가 각 후처리 패스마다 존재하고, **변이로 이빨이 증명되어 있다**

### 트랙 V

- [ ] 정점 레이아웃 오프셋이 코드에 손으로 박힌 곳 **0** — 전부 기술표에서 유도
- [x] 트랙 V 의 변경이 legacy 파일을 건드리지 않는다(`::Vertex`·`Mesh.h`·legacy `ModelLoader` diff 0)
- [x] 레이아웃 버전이 **기술표에서 유도**된다 — 속성/포맷을 바꾸면 손대지 않아도 달라진다
- [x] 후처리 패스에 스트림 이름이 손으로 나열된 곳 0 — 새 스트림 추가가 그 파일을
      건드리지 않는다
- [x] `streams.colors` 처럼 **생산되고 소비되지 않는 스트림 0건**
- [x] 정적 메시가 스킨 바이트를 내지 않는다 — 48B 정적 실자산 확인, 6.39MB는 감사 기반 누적 전망
- [ ] 무손실 구간(V2)에서 **픽셀 차이 0** — 양자화가 아니므로 근사 허용 없음
- [ ] 입력 레이아웃과 셰이더 `VSIn` 의 짝 어긋남을 **검사가 잡는다**(변이로 증명)
- [ ] UV1 이 라이트맵 대상 메시에만 붙는다(트랙 L 연동) — 전 정점 부과 0

---

## 6. 리스크

| 리스크 | 근거 | 완화 |
|---|---|---|
| 소비자 0 상태 장기화 | 이 저장소에 죽은 추상 전례 있음 | V2/V3·M5/M6·식별자 게이트 뒤 I5를 첫 생산 소비자로 잇는다 |
| 용접이 탄젠트를 깬다 | mikktspace 규약(§I4) | 탄젠트를 정점 정체성에 포함 |
| 실자산 게이트의 맹점 | 변이 실험으로 확인됨(§3.4) | 패스마다 합성 검사 필수 |
| 전환 중 좌표 규약 회귀 | FBX 에서 실제로 발생(1.501) | AABB 실값 대조 상시 |
| 계획과 실행이 어긋남 | MaterialPipelinePlan §0 전례 | 착수 전 §1 재실측 |
| **캐시 조용한 오독** | experiment cooked 포맷은 I7에 신설됐고 V2/V3에서 레이아웃이 바뀐다 | V2/V3가 같은 V1 표에서 hash/mask/stride를 유도·검사하도록 닫았다 |
| **legacy 에 투자한다** | I6 에서 은퇴할 코드다 | 트랙 V 의 대상을 experiment 로 못박았다(§4-V 서문) |
| **레이아웃 변경이 화면을 조용히 깬다** | 오프셋이 5곳 · `VSIn` 4곳에 흩어짐 | V1 기술표로 단일화 후에만 V2 착수 |
| **I5 치환 뒤 레이아웃 변경 비용 급증** | 렌더 경로 10파일이 묶인다 | **V2/V3 완료. V4를 I5와 묶는다** |

★ 마지막 항이 순서 제약이다. V2/V3를 생산 소비자 0인 상태에서 완료했다. 이제
I5(치환)와 V4에서 입력 레이아웃 5곳·셰이더 4개를 함께 전환한다.

---

## 7. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| **`ScriptableRenderPipelinePlan` (같은 PHASE 4)** | **V4 가 그쪽 Asset-first Pass 계약의 전제다.** Pass 가 레이아웃 오프셋을 C++ 에 박고 있으면 Pass 를 Asset 으로 기술할 수 없다. 경계는 §0 |
| **PHASE 4 GPU-driven** | 트랙 V 의 첫 대형 소비자. `buildMeshlets` 보류 사유가 여기서 해소된다 |
| **PHASE 4 DXR** | BLAS 가 정점 포맷·stride 를 직접 받는다 — V2/V3 의 절감이 가속 구조에 그대로 간다 |
| SerializationPlan (PHASE 17) | I2·I7 이 `.meta`/AssetId 발급과 쿠킹 결정을 공유. **V0 의 캐시 버전 규약도 그쪽 형식 결정을 따른다** |
| SceneGraphRedesignPlan | I5 안 B 가 노드/엔티티 표현과 충돌하는지 확인 필요 |
| MaterialPipelinePlan (PHASE 3.5) | I5-0에서 표준 PBR property 이름을 공유했다. M5가 generation/소유 경계를, M6-P0~P2d-e가 GBuffer/Forward의 property·texture·ShaderMeta/PSO·Foliage/flow·required-asset와 legacy 은퇴까지 닫아 PHASE 3.5는 완료됐다. D5-b2a 단일 model producer, D5-b2b1 model 전수 Cook, D5-b2b2 제품 pak 게시는 섰다. I5는 D2/D5-a/D5-b1 계약 위에 D5-b2c 나머지 ID가 공급된 뒤 `experiment::Material`을 그 계약에 직접 연결한다. **V4의 퍼뮤테이션 축도 그쪽 키 체계를 쓴다** |
| **`LightmapBakerPlan` (같은 PHASE 4 · 트랙 L)** | **V3 옵셔널 스트림의 첫 소비자.** V6 는 그쪽으로 이관됐다. 삭제된 베이커가 uv1 의 원래 소비자였다(§1.8) |
| AnimationSchedulerPlan (PHASE 13) | 게시된 clip 의 소비자가 그쪽 평가 엔진이다 |
| `RhiBoundaryPlan` (PHASE 3) | V5(멀티 슬롯)가 `RHIEncoder`·`RHIMeshBinding` 확장을 요구 — 보류 중 |
