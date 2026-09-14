"""PBR-W1 fixture 생성기 — Tools/regression/fixtures/pbr-normal-pair/.

손으로 못 고치는 것 둘(.bin 오프셋, PNG 바이트)을 만든다. 이 스크립트는
fixture README 가 가리키는 재생성 절차의 정본이다.

쿼드 둘 · 재질 둘: WithNormal 만 normalTexture 를 갖는다. 한 mesh 의 두
primitive 라 한 노드·한 카메라 아래서 draw 둘이 같은 프레임에 뜬다 —
대조쌍이 캡처 하나 안에서 성립해야 useNormalMap 판정이 성립한다.
"""
import struct, zlib, pathlib, sys

out = pathlib.Path(sys.argv[1])
(out / "Textures").mkdir(parents=True, exist_ok=True)

# ── geometry ──────────────────────────────────────────────────────────
# 왼쪽 쿼드가 normal map 을 받는 쪽, 오른쪽이 안 받는 쪽이다.
QUADS = [(-1.1, -0.1), (0.1, 1.1)]
positions, normals, uvs, indices = [], [], [], []
for qi, (x0, x1) in enumerate(QUADS):
    base = qi * 4
    positions += [(x0, -0.5, 0.0), (x1, -0.5, 0.0), (x1, 0.5, 0.0), (x0, 0.5, 0.0)]
    normals += [(0.0, 0.0, 1.0)] * 4
    uvs += [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    indices += [base + 0, base + 1, base + 2, base + 0, base + 2, base + 3]

idx_bytes = b"".join(struct.pack("<H", i) for i in indices)
pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
uv_bytes = b"".join(struct.pack("<2f", *t) for t in uvs)

# bufferView 오프셋은 component size 의 배수여야 한다(ushort 2 · float 4).
off_idx = 0
off_pos = off_idx + len(idx_bytes)          # 24
off_nrm = off_pos + len(pos_bytes)          # 120
off_uv = off_nrm + len(nrm_bytes)           # 216
assert off_pos % 4 == 0 and off_nrm % 4 == 0 and off_uv % 4 == 0
blob = idx_bytes + pos_bytes + nrm_bytes + uv_bytes
(out / "NormalPair.bin").write_bytes(blob)

# ── normal map ────────────────────────────────────────────────────────
# 평평한 (128,128,255) 로 두면 노멀맵을 물려도 결과가 안 물린 것과 같아져
# 대조가 죽는다. 좌우를 반대 방향으로 기울여 확실히 갈라 둔다.
W = H = 4
TILT_POS = (204, 128, 229, 255)   # +X 로 기운 tangent normal
TILT_NEG = (51, 128, 229, 255)    # -X 로 기운 tangent normal
raw = bytearray()
for y in range(H):
    raw.append(0)  # PNG filter type 0
    for x in range(W):
        raw += bytes(TILT_POS if x < W // 2 else TILT_NEG)


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
       + chunk(b"IEND", b""))
(out / "Textures" / "Normal.png").write_bytes(png)

print(f"bin={len(blob)}B offsets={off_idx}/{off_pos}/{off_nrm}/{off_uv} png={len(png)}B")
