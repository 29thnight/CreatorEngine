# sampler 삼종 대조 fixture

`PBR-W7`(`docs/plans/PBRWiringStabilizationPlan.md` §19) 전용.
손으로 만든 것이고 외부 자산이 아니다 — 라이선스 의무가 없다.

| 파일 | 무엇인가 |
|---|---|
| `SamplerModes.gltf` | 쿼드 3개 · 재질 3개 · texture 3개가 **이미지 하나**를 sampler 3종으로 참조. |
| `SamplerModes.bin` | 인덱스 18 + 위치·법선·UV 각 12정점(420바이트). |
| `Quadrants.png` | 4×4 RGB. 사분면마다 다른 색(88바이트). |
| `make_sampler.py` | `.bin` 과 `.png` 생성기. |

| sampler | wrapS | wrapT | magFilter | minFilter |
|---|---|---|---|---|
| `RepeatLinear` | REPEAT | REPEAT | LINEAR | LINEAR_MIPMAP_LINEAR |
| `ClampSRepeatT` | CLAMP_TO_EDGE | REPEAT | LINEAR | LINEAR_MIPMAP_LINEAR |
| `MirrorNearest` | MIRRORED_REPEAT | MIRRORED_REPEAT | NEAREST | NEAREST_MIPMAP_NEAREST |

## 이미지를 하나만 둔 이유

임포터의 텍스처 캐시가 **`imageIndex` 단독 키**다. glTF 에서 `texture` 는
`{ source, sampler }` 라 같은 이미지를 sampler 만 달리해 여러 번 참조하는 것이
정상인데, 이미지 키로 접으면 셋이 하나가 된다. 그래서 샘플러는
`ImportedTexture`(이미지) 가 아니라 `TextureSlot`(참조) 에 실어야 하고, 이
fixture 가 그 선택을 자극한다. Khronos `TextureSettingsTest` 도 같은 모양이다
(image 3 개를 texture 9 개가 나눠 쓴다) — 그쪽은 `Dynamic_CPP/Assets/Models/`
아래 추적 밖으로 두고 눈으로 보는 용도로 쓴다.

## UV 를 0..2 로 둔 이유

`[0,1]` 안에만 있으면 wrap 모드가 무엇이든 결과가 같아서 **wrap 을 아예 읽지 않는
회귀가 통과한다.** 경계 밖으로 나가야 REPEAT 는 반복하고 CLAMP 는 늘어나고
MIRROR 는 뒤집힌다.

## 이미지를 비대칭으로 그린 이유

좌우 대칭이면 MIRROR 와 REPEAT 가 같은 그림이 되어, **Mirror 를 Wrap 으로 접는
회귀가 통과한다** — `RHIAddressMode` 에 `Mirror` 가 없던 시절의 동작이 바로
그것이었다(DX12 는 WRAP, Vulkan 은 CLAMP_TO_EDGE 로 서로 다르게 접혔다).
사분면마다 색을 달리해 뒤집힘이 보이게 한다.

## 이 fixture 가 자극하는 축과 자극하지 못하는 축

자극한다:

- 재질별 sampler — 장부의 `samplerIdentity` 가 draw 마다 갈린다(3종).
- `MIRRORED_REPEAT` — 새 `RHIAddressMode::Mirror` 열거자의 유일한 소비자다.
- Point 필터 — 패스가 고정으로 걸던 `Linear/Linear` 와 다른 값이다.
- 이미지 공유 — 위 참조.

자극하지 **못한다**(덮었다고 주장하지 않는다):

- **재질 안 슬롯별 분기.** 재질마다 baseColor 하나뿐이라 한 재질 안에서
  baseColor 와 normal 이 다른 sampler 를 갖는 경우가 없다. 셰이더에
  `gSampler : register(s0)` 하나뿐이라 지금은 표현할 수도 없고,
  `MaterialTextureTable::EffectiveSampler` 가 "가장 낮은 레지스터가 이긴다"로
  못 박아 두었다. 저장소 자산 어디에도 이 분기가 없다(`.gltf` 3종 · 텍스처 둘
  이상인 재질 2건 · 분기 0건).
- **Anisotropic.** glTF 코어에 없고(`KHR_texture_basisu` 도 아니다) RHI 어휘에도
  없다. 어휘 구멍으로만 기록한다.

## 다시 만들려면

`.bin` 과 `.png` 는 생성물이다. 손으로 고치지 말고 생성기로 다시 만든다 —
`.gltf` 의 `bufferViews` 오프셋(0/36/180/324)과 기재가 맞아야 한다.

```
python3 Tools/regression/fixtures/pbr-sampler/make_sampler.py Tools/regression/fixtures/pbr-sampler
# bin=420B offsets=0/36/180/324 png=88B
```

출력의 수가 위와 다르면 `.gltf` 의 `bufferViews`·`byteLength`·`accessors` 도 함께
고쳐야 한다. `.gltf` 는 손으로 쓴 것이라 생성기가 건드리지 않는다.
