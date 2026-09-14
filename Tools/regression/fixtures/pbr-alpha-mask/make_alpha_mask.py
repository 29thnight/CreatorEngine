"""PBR-W0 fixture 생성기 — Tools/regression/fixtures/pbr-alpha-mask/.

쿼드 셋 · 재질 셋: OPAQUE · MASK(cutoff 0.5) · BLEND 가 **같은 baseColor 텍스처**를
쓴다. 텍스처의 알파가 4 분면으로 0.0/0.25/0.75/1.0 이라, cutoff 0.5 를 제대로 물면
MASK 쿼드에서 절반이 사라지고 OPAQUE 는 그대로 · BLEND 는 섞인다.

★ 알파를 0/1 로만 두지 않은 이유: 그러면 cutoff 값이 0.1 이든 0.9 든 결과가 같아
  "cutoff 를 아예 안 읽는" 회귀가 통과한다. 0.25 와 0.75 를 넣어 0.5 를 사이에 둔다.

★ 셋을 한 자산에 둔 이유는 normal-pair 와 같다 — 같은 노드·같은 프레임·같은 기하
  아래라야 변인이 blend mode 하나로 좁혀진다.
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
    uvs += [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
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
(out / "AlphaModes.bin").write_bytes(blob)

# 4x4 RGBA. 알파는 사분면마다 0 / 64 / 191 / 255 (= 0.0 / 0.25 / 0.75 / 1.0).
W = H = 4
ALPHA = [[0, 64], [191, 255]]
raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        a = ALPHA[0 if y < H // 2 else 1][0 if x < W // 2 else 1]
        raw += bytes((220, 90, 60, a))


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
       + chunk(b"IEND", b""))
(out / "AlphaSteps.png").write_bytes(png)

print(f"bin={len(blob)}B offsets=0/{off_pos}/{off_nrm}/{off_uv} png={len(png)}B")
