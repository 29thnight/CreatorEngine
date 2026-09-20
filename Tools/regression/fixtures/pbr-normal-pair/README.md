# normal-map 유무 대조쌍 fixture

`PBR-W1`(`docs/plans/PBRWiringStabilizationPlan.md`) 전용. 손으로 만든 것이고 외부
자산이 아니다 — 라이선스 의무가 없다.

| 파일 | 무엇인가 |
|---|---|
| `NormalPair.gltf` | 쿼드 2개 · 재질 2개. `WithNormal` 만 `normalTexture` 를 갖는다. |
| `NormalPair.bin` | 인덱스 12 + 위치·법선·UV 각 8정점(280바이트). |
| `Textures/Normal.png` | 4×4 RGBA tangent normal(77바이트). |

## 왜 대조쌍이 **한 자산 안에** 있어야 하나

W1 의 판정은 "노멀맵이 있는 draw 와 없는 draw 가 서로 다르게 다뤄지는가" 다. 이것을
자산 둘로 나누어 두면 카메라·광원·프레임·씬 epoch 이 달라질 수 있고, 그러면 차이가
노멀맵 때문인지 다른 것 때문인지 가릴 수 없다. 한 mesh 의 두 primitive 로 두면 같은
노드·같은 변환·같은 프레임 아래서 draw 둘이 뜨므로 **변인이 재질 하나**로 좁혀진다.

`POSITION`/`NORMAL`/`TEXCOORD_0` accessor 를 두 primitive 가 공유하고 인덱스만
가른다(accessor 0 은 정점 0..3, accessor 4 는 4..7). 기하까지 같은 출처를 쓰게 해
"기하가 달라서 생긴 차이" 라는 해석을 없애려는 것이다.

## 왜 저장소가 이 fixture를 직접 소유하나

저장소가 추적하는 모델은 `Prim_*` 9개뿐이고 **전부 `normalTexture` 가 0건**이다
(2026-09-14 전수 확인). 노멀맵을 가진 것은 `Gunner_F_Mythic.glb` 인데 그 파일은
`.gitignore` 의 `/Dynamic_CPP/Assets/Models/*` 에 막혀 **추적 밖**이다 — 한 기계의
디스크에만 있다. 그것만 쓰는 게이트는 clean checkout 에서 조용히 비어 버린다
(`Tools/regression/fixtures/imgui-ini` · `gltf-multifile` 이 같은 이유로 생겼다).

현재 `verify-pbr-wiring-baseline.ps1`의 스킨드·다중 재질 축은
CreatorRobot으로 교체했다. 이 모델도 로컬 준비본이며 없으면 skipped로 기록한다.
[CreatorRobot fixture 안내](../creator-robot/README.md)를 따른다. 이 대조쌍은
계속 clean checkout에서 노멀맵 유무만 독립적으로 검증한다.

## 평평한 노멀맵을 쓰지 않는 이유

`(128,128,255)` 로 채우면 디코드 결과가 `(0,0,1)` 이라 노멀맵을 **물리지 않은 것과
같은 픽셀**이 나온다. 그러면 "노멀맵이 안 물렸다" 는 회귀가 통과한다. 좌우를 각각
+X/−X 로 기울여 두어, 물렸을 때와 안 물렸을 때가 반드시 다른 값이 되게 했다.

## 다시 만들려면

`NormalPair.bin` 과 `Textures/Normal.png` 는 생성물이다. 손으로 고치지 말고 옆에 있는
`make_normal_pair.py` 로 다시 만든다 — `.bin` 의 `bufferViews` 오프셋(0/24/120/216)과
`.gltf` 의 기재가 맞아야 하고, 손으로 맞추면 어긋난다.

```
python3 Tools/regression/fixtures/pbr-normal-pair/make_normal_pair.py Tools/regression/fixtures/pbr-normal-pair
# bin=280B offsets=0/24/120/216 png=77B
```

출력의 세 수가 위와 다르면 `.gltf` 의 `bufferViews`·`byteLength` 도 함께 고쳐야 한다.
`.gltf` 는 손으로 쓴 것이므로 생성기가 건드리지 않는다.
