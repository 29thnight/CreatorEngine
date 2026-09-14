"""PBR-W7 fixture 생성기 — Tools/regression/fixtures/pbr-sampler/.

쿼드 셋이 **같은 이미지 하나**를 서로 다른 sampler 로 참조한다. 이 모양이어야
하는 이유가 셋이다:

★ 임포터의 텍스처 캐시가 `imageIndex` 단독 키다. 같은 이미지를 sampler 만 달리해
  참조하는 것이 glTF 에서는 정상인데(texture = { source, sampler }), 이미지 키로
  접으면 셋이 하나가 된다. 샘플러를 ImportedTexture 가 아니라 TextureSlot 에
  실어야 하는 이유가 여기 있고, 이 fixture 가 그 선택을 자극한다.

★ UV 를 0..2 로 둔다. [0,1] 안에 있으면 wrap 모드가 무엇이든 결과가 같아서
  **wrap 을 아예 읽지 않는 회귀가 통과한다.** 경계 밖으로 나가야 REPEAT 는
  반복하고 CLAMP 는 늘어나고 MIRROR 는 뒤집힌다.

★ 이미지를 비대칭으로 그린다. 좌우 대칭이면 MIRROR 와 REPEAT 가 같은 그림이 되어
  Mirror 를 Wrap 으로 접는 회귀(= RHIAddressMode 에 Mirror 가 없던 시절의 동작)가
  통과한다. 사분면마다 색을 달리해 뒤집힘이 보이게 한다.
"""
import struct, zlib, pathlib, sys

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

QUADS = [(-1.65, -0.55), (-0.45, 0.45), (0.55, 1.65)]
positions, normals, uvs, indices = [], [], [], []
for qi, (x0, x1) in enumerate(QUADS):
    base = qi * 4
    positions += [(x0, -0.5, 0.0), (x1, -0.5, 0.0), (x1, 0.5, 0.0), (x0, 0.5, 0.0)]
    normals += [(0.0, 0.0, 1.0)] * 4
    # 0..2 — 타일이 축마다 두 번 나온다.
    uvs += [(0.0, 2.0), (2.0, 2.0), (2.0, 0.0), (0.0, 0.0)]
    indices += [base + 0, base + 1, base + 2, base + 0, base + 2, base + 3]

idx = b"".join(struct.pack("<H", i) for i in indices)
pos = b"".join(struct.pack("<3f", *p) for p in positions)
nrm = b"".join(struct.pack("<3f", *n) for n in normals)
uv = b"".join(struct.pack("<2f", *t) for t in uvs)
off_pos = len(idx)
off_nrm = off_pos + len(pos)
off_uv = off_nrm + len(nrm)
assert off_pos % 4 == 0 and off_nrm % 4 == 0 and off_uv % 4 == 0
blob = idx + pos + nrm + uv
(out / "SamplerModes.bin").write_bytes(blob)

# 4x4 RGB. 사분면마다 다른 색 — 좌우/상하 어느 쪽으로도 대칭이 아니다.
W = H = 4
QUAD_COLOR = [[(230, 60, 40), (250, 210, 40)],    # 위: 빨강 | 노랑
              [(40, 90, 220), (30, 170, 90)]]     # 아래: 파랑 | 초록
raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        raw += bytes(QUAD_COLOR[0 if y < H // 2 else 1][0 if x < W // 2 else 1])


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
       + chunk(b"IEND", b""))
(out / "Quadrants.png").write_bytes(png)

print(f"bin={len(blob)}B offsets=0/{off_pos}/{off_nrm}/{off_uv} png={len(png)}B")
