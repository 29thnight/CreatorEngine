# pbr-shared-material — PBR-W8 밀봉 신원 fixture

메시(=glTF primitive) **둘이 재질 하나를 공유**하는 최소 자산이다. 생성기는
[`make_shared_material.py`](make_shared_material.py)이고, 산출물(`.gltf`/`.bin`)도 함께
추적한다 — 추적 밖 fixture 는 게이트를 조용히 비운다.

| 파일 | 크기 | 내용 |
|---|---:|---|
| `SharedMaterial.gltf` | ~1.5K | 노드 1 · 메시 1 · **primitive 2 · material 1** |
| `SharedMaterial.bin` | 280B | 인덱스 24B + 위치 96B + 법선 96B + UV 64B |

## 왜 이 모양인가

**씬 저작으로는 이 상태를 만들 수 없다.** `MeshRenderer` 역직렬화의 세 표기가 모두
렌더러마다 자기 `Material` 사본을 만든다.

| 표기 | `m_Material` |
|---|---|
| ref (base+diff) | `make_shared<Material>(*base)` — 사본 |
| 인라인 새 정본 | `make_shared<Material>()` — 사본 |
| legacy | typed 역직렬화의 자기 객체 |

`DataSystem::LoadMaterialShared` 는 **base 를 얻어 복사하는 데만** 쓰인다. 그래서
"씬에 재질 하나를 둘이 참조하게 적는다" 로는 W8 이 고친 결함을 **자극하지 못한다** —
주소가 애초에 다르기 때문이다.

**살아 있는 공유 경로는 모델 인스턴스화 하나다**(`ModelSceneInstantiation.cpp`):

```cpp
renderer->SetMaterial(state.materials[materialIndex]);              // 주소 공유
renderer->SetExperimentMaterialBase(state.authored[materialIndex]);  // base 공유
```

같은 `materialIndex` 를 쓰는 메시 둘이면 두 렌더러가 **같은 `Material*`** 을 받고,
각자 그 base 를 감싸는 **자기 `MaterialInstance`** 를 갖는다
(`SetExperimentMaterialBase` 가 렌더러마다 새로 만든다). 그 인스턴스에 서로 다른
override 를 얹으면 **주소는 같은데 값이 다른** 상태가 된다 — W8 이 밀봉 중복 제거 키를
주소에서 값으로 바꾼 바로 그 이유다.

## 쓰는 법

```
model.load  <이 폴더>/SharedMaterial.gltf
model.place SharedMaterial
material.override <렌더러A> roughness 0.10
material.override <렌더러B> roughness 0.90
render.pbr.capture ...
```

`material.override` 가 `MaterialInstance::SetPropertyOverride` 까지 닿는 유일한
헤드리스 표면이다 — 그 전에는 GUI 인스펙터와 C# 스크립트뿐이라 게이트가 이 상태를
만들 수 없었다.

## 관측값 — 변이 전에 먼저 정했고, 2026-09-16 에 실측으로 확인했다

| | distinct sealHash | distinct authoredDigest | bindingConflict |
|---|---:|---:|---:|
| 기준 | **2** | 2 | 0 |
| 변이(밀봉 키에서 `authoredDigest` 제거) | **1** | **1** | **0 — 안 운다** |

실측(Debug · dx12 · `verify-pbr-wiring-baseline.ps1`):

| | meshId | sealHash | authoredDigest |
|---|---|---:|---:|
| 렌더러 0 (roughness 0.10) | `0b67047e…` | 4540832424283382145 | 14302327363989219122 |
| 렌더러 1 (roughness 0.90) | `29792225…` | 16793130829285904082 | 78459466700331823 |

변이 회차에서 **두 draw 가 모두 `4540832424283382145`** 를 받았다 — 기준 회차의
렌더러 0 값 그대로다. 둘째가 첫째의 스냅샷을 받았다는 직접 증거이며, 장부는
`bindingConflict 0 · skipped 0 · valueMismatch 0 · violations 0 · lastReason ''` 로
**완전히 침묵**했다.

★ 단정을 **conflict 가 아니라 distinct 수**에 걸어야 한다. 키가 주소로 돌아가면 둘째
draw 가 첫째의 스냅샷을 **그대로 받으므로** 바인딩이 일치해 장부는 침묵한다. PBR-W7
때와 정확히 반대 모양이다(그때는 conflict 가 울고 distinct 가 침묵했다) — 미리 정하지
않으면 "게이트가 눈멀었다" 로 오진한다.

## 텍스처가 없는 이유

재는 축이 픽셀이 아니라 **신원**이다. 텍스처를 넣으면 texture table 이 함께 움직여
digest 가 갈린 이유를 못 가른다. 변하는 것을 override 하나로 좁힌다.

쿼드 둘은 좌우로 벌려 뒀다 — 겹치면 어느 draw 가 살아남았는지 사람이 눈으로 못 가른다.
