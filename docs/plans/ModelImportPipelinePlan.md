# 모델 임포트 파이프라인 (PHASE 24)

2026-08-25 신설. glTF·FBX 임포터와 그 사이 계층이 이미 서 있고 게이트도 도는데,
**계획 문서만 없었다.** 지시 단위로 진행돼서 단계 정의·완료 기준·순서 제약이
어디에도 적혀 있지 않다. 다른 페이즈는 전부 문서를 갖고 있는데 이것만 없었다.

목표 사슬:

```
파일 → IAssetImporter → ImportedScene(IR) → [후처리] → ModelDraft → ModelLoader → Model
        gltf / fbx                        탄젠트·용접·LOD    ↑ 손실은 여기 한 곳
```

---

## 1. 지금 무엇이 있는가 — 측정 (2026-08-25)

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

### 1.2 실물 디코더가 없다

`IModelDecoder` 구현체는 **하나뿐이고 그것은 테스트 하네스다**
(`LegacyBridgeDecoder` — 미리 만든 draft 를 복사해 돌려준다).

즉 `ModelLoader`(검증 → 단일 move 게시)와 임포터 사이가 **배선되어 있지 않다.**
검사는 손으로 잇는다: 임포트 → `ConvertToModelDraft` → draft 를 하네스 디코더에
싸서 `Load`. 생산 경로에는 그 접착제가 아예 없다.

`ModelSourcePreference` 는 `CookedThenSource`·`SourceOnly`·`CookedOnly` 셋을
선언하는데 **cooked 디코더가 없어 둘은 쓸 수 없다.**

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

### 1.6 서 있는 것 (완료된 자산)

| 계층 | 파일 | 상태 |
|---|---|---|
| 데이터 모델 | `Experiment/ModelData.h`, `Model.*`, `ModelLoader.*` | 검증·게시 완결 |
| 임포트 IR | `Import/ImportedScene.*` | 계약 검증기 포함 |
| 생산자 | `Import/GltfImporter.*`, `Import/FbxImporter.*` | 2종 |
| 후처리 | `Import/TangentGeneration.*` | mikktspace |
| 변환 경계 | `Import/SceneToModelDraft.*` | 손실 단일점 |
| 벤더링 | `ThirdParty/{fastgltf, ufbx, mikktspace}` | 원본 무수정 |
| 게이트 | `experiment.{model,anim,bench,import,gltf,fbx,sampler,tangent}` | 8종 |

---

## 2. 무엇을 하려는가

1. **legacy(Assimp) 를 이 경로로 대체한다.** 소비자 0 상태를 끝내는 것이 이
   페이즈의 본체다.
2. **남은 손실점을 닫는다.** 텍스처 해석·노멀 생성·meshopt.
3. **"손실은 한 경계에서만, 반드시 계수된다" 성질을 유지한다.** 이것이
   `ImportedScene` 을 따로 둔 이유이고, 전환 과정에서 깨지면 안 된다.

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

### I5. 런타임 어댑터 — **결정 필요**

`experiment::Model` 을 렌더러가 소비하는 형태로 잇는다. 선택지가 둘이고 아직
정하지 않았다.

| 안 | 내용 | 대가 |
|---|---|---|
| A. 어댑터 | `experiment::Model` → 기존 `::Model` 변환 | 두 표현 유지, 변환 비용 |
| B. 치환 | 렌더 경로가 `experiment::Model` 을 직접 소비 | 소비자 10파일 수정, 되돌리기 어려움 |

legacy `Model` 소비 파일은 **10개**로 측정됐다. B 가 가능한 규모다.
SceneGraphRedesignPlan(PHASE 16 계열) 과 충돌 여부를 먼저 확인해야 한다.

### I6. 전환 — Assimp 은퇴

I5 결정 후. 두 경로 병행 기간을 두고 픽셀·성능 대조를 거친 뒤 legacy 로더를
제거한다. **Release 로만 성능을 판정한다**(Debug 는 같은 조건에서 25배 느리고
규모별 개선 방향까지 뒤집는다).

### I7. cooked 경로

`ModelPayloadKind::Cooked` 와 `ModelSourcePreference::CookedOnly` 가 선언만 되어
있다. SerializationPlan(PHASE 17) 의 런타임 쿠킹과 같은 결정을 공유한다.

### I8. 검사 자산·게이트 보강

- 값이 변하는 Step 트랙을 가진 자산 확보(또는 합성 자산 생성)
- 실자산 게이트에 "코너 탄젠트가 자기 삼각형 UV 기울기와 부호가 맞는가" 추가
  — **정상 코드에서 기준값을 먼저 재고 임계를 정한다.** 평활화된 이음매에서
  거짓 실패가 날 수 있다.
- `Cha_Mon_5.fbx` 처럼 기준선 없는 자산의 판정 방식

---

## 5. 완료 기준

- [ ] 생산 경로가 `IAssetImporter` 를 탄다 — legacy 로더 호출 지점 0
- [ ] 게이트가 **실물 디코더**를 타고 돈다(하네스 전용 디코더 은퇴)
- [ ] `ImportOptions` 플래그 중 소비자 0 인 것이 `buildMeshlets` 뿐
- [ ] 임베디드 텍스처가 자산으로 해석된다 — property 생략 0건
- [ ] 대표 자산 N종에서 legacy 대비 픽셀 대조 통과
- [ ] Release 성능이 legacy 대비 회귀 없음
- [ ] 합성 검사가 각 후처리 패스마다 존재하고, **변이로 이빨이 증명되어 있다**

---

## 6. 리스크

| 리스크 | 근거 | 완화 |
|---|---|---|
| 소비자 0 상태 장기화 | 이 저장소에 죽은 추상 전례 있음 | I1 을 최우선 — 배선이 먼저다 |
| 용접이 탄젠트를 깬다 | mikktspace 규약(§I4) | 탄젠트를 정점 정체성에 포함 |
| 실자산 게이트의 맹점 | 변이 실험으로 확인됨(§3.4) | 패스마다 합성 검사 필수 |
| 전환 중 좌표 규약 회귀 | FBX 에서 실제로 발생(1.501) | AABB 실값 대조 상시 |
| 계획과 실행이 어긋남 | MaterialPipelinePlan §0 전례 | 착수 전 §1 재실측 |

---

## 7. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| SerializationPlan (PHASE 17) | I2·I7 이 `.meta`/AssetId 발급과 쿠킹 결정을 공유 |
| SceneGraphRedesignPlan | I5 안 B 가 노드/엔티티 표현과 충돌하는지 확인 필요 |
| MaterialPipelinePlan (PHASE 3.5) | 변환 경계의 shader property 이름 매핑이 `.shadermeta` 정본을 따른다 |
| AnimationSchedulerPlan (PHASE 13) | 게시된 clip 의 소비자가 그쪽 평가 엔진이다 |
