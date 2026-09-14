# alpha mode 삼종 대조 fixture

`PBR-W0`(`docs/plans/PBRWiringStabilizationPlan.md` §4 의 fixture 목록) 전용.
손으로 만든 것이고 외부 자산이 아니다 — 라이선스 의무가 없다.

| 파일 | 무엇인가 |
|---|---|
| `AlphaModes.gltf` | 쿼드 3개 · 재질 3개(`OPAQUE` · `MASK` cutoff 0.5 · `BLEND`). |
| `AlphaModes.bin` | 인덱스 18 + 위치·법선·UV 각 12정점(420바이트). |
| `AlphaSteps.png` | 4×4 RGBA. 알파가 사분면마다 0 / 0.25 / 0.75 / 1.0(84바이트). |
| `make_alpha_mask.py` | `.bin` 과 `.png` 생성기. |

## 알파를 0/1 로만 두지 않은 이유

그렇게 두면 cutoff 가 0.1 이든 0.9 든 결과가 같아서, **cutoff 를 아예 읽지 않는
회귀가 통과한다.** 0.25 와 0.75 를 넣어 0.5 를 사이에 두었다 — 제대로 물면 MASK
쿼드에서 절반이 사라지고, 못 물면 전부 남거나 전부 사라진다.

## 셋을 한 자산에 둔 이유

`pbr-normal-pair` 와 같다. 같은 노드·같은 프레임·같은 기하·같은 텍스처 아래라야
**변인이 blend mode 하나**로 좁혀진다. 세 primitive 가 `POSITION`/`NORMAL`/
`TEXCOORD_0` accessor 를 공유하고 인덱스만 가른다.

## 이 fixture 가 겸하는 축 둘

1. **alpha mask** — 실측 결과 coverageFlags 가 셋으로 갈린다(`1` · `11` · `5`).
2. **forward 라우트** — `BLEND` 재질이 **forward 로 간다**(실측 draw 11 중
   forward 1 · gbuffer 10). 이것이 중요한 이유는 따로 있다: 캡처의 draw↔바인딩
   조인을 **라우트별로** 하도록 게이트가 짜여 있는데(§18), 여기 전까지 모든
   fixture 의 draw 가 gbuffer 라 **forward 장부가 늘 비어 있었다** — 두 장부를
   합쳐 찾는 잘못된 구현도 통과했을 상태였다. 이 fixture 가 그 축을 처음 자극한다.

비균등 스케일 축은 자산이 아니라 **씬 변환**이라 게이트가
`object.transform ... 1.7 0.6 1.0` 로 건다(공짜다).

## 다시 만들려면

`.bin` 과 `.png` 는 생성물이다. 손으로 고치지 말고 생성기로 다시 만든다 —
`.gltf` 의 `bufferViews` 오프셋(0/36/180/324)과 기재가 맞아야 한다.

```
python3 Tools/regression/fixtures/pbr-alpha-mask/make_alpha_mask.py Tools/regression/fixtures/pbr-alpha-mask
# bin=420B offsets=0/36/180/324 png=84B
```

출력의 수가 위와 다르면 `.gltf` 의 `bufferViews`·`byteLength`·`accessors` 도 함께
고쳐야 한다. `.gltf` 는 손으로 쓴 것이라 생성기가 건드리지 않는다.
