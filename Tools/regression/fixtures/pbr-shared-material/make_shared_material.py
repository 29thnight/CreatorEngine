"""PBR-W8 fixture 생성기 — Tools/regression/fixtures/pbr-shared-material/.

메시(=primitive) **둘이 재질 하나를 공유**한다. 이 모양이어야 하는 이유는 하나뿐이고,
그 하나가 W8 의 핵심 수정 전체를 지탱한다.

★ **씬 저작으로는 공유 주소가 안 생긴다.** MeshRenderer 역직렬화의 세 표기가 모두
  렌더러마다 자기 `Material` 사본을 만든다 — ref 표기는 `make_shared<Material>(*base)`,
  인라인 새 정본은 `make_shared<Material>()`, legacy 는 typed 역직렬화의 자기 객체다.
  `DataSystem::LoadMaterialShared` 는 **base 를 얻어 복사하는 데만** 쓰인다.
  그래서 "씬에 재질 하나를 둘이 참조하게 적는다" 로는 W8 이 고친 결함을 **자극하지
  못한다**(주소가 애초에 다르다).

★ **살아 있는 공유 경로는 모델 인스턴스화 하나다.** ModelSceneInstantiation 이
  메시마다 MeshRenderer 를 만들면서 이렇게 붙인다:

      renderer->SetMaterial(state.materials[materialIndex]);             // 주소 공유
      renderer->SetExperimentMaterialBase(state.authored[materialIndex]); // base 공유

  같은 `materialIndex` 를 쓰는 메시 둘이면 두 렌더러가 **같은 `Material*`** 을 받고,
  각자 그 base 를 감싸는 **자기 `MaterialInstance`** 를 갖는다. 그 인스턴스에 서로 다른
  override 를 얹으면 주소는 같은데 값이 다른 상태가 된다 — W8 이 밀봉 키를 주소에서
  값으로 바꾼 바로 그 이유다.

★ 그래서 이 fixture 는 **텍스처를 쓰지 않는다.** 재는 축이 픽셀이 아니라 신원이라,
  변하는 것을 override 하나로 좁힌다. 텍스처를 넣으면 texture table 이 함께 움직여
  digest 가 갈린 이유를 못 가른다.

기하는 쿼드 둘을 좌우로 벌려 둔다. 겹치면 캡처에서 어느 draw 가 살아남았는지 눈으로
못 가른다(장부 수와 별개로, 사람이 확인할 수 있어야 한다).
"""
import json
import pathlib
import struct
import sys

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

# 쿼드 둘 — 좌/우. z 는 0, 법선은 +Z.
QUADS = [(-1.1, -0.1), (0.1, 1.1)]
positions, normals, uvs, index_sets = [], [], [], []
for qi, (x0, x1) in enumerate(QUADS):
    base = qi * 4
    positions += [(x0, -0.5, 0.0), (x1, -0.5, 0.0), (x1, 0.5, 0.0), (x0, 0.5, 0.0)]
    normals += [(0.0, 0.0, 1.0)] * 4
    uvs += [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    index_sets.append([base + 0, base + 1, base + 2, base + 0, base + 2, base + 3])

idx = b"".join(struct.pack("<H", i) for s in index_sets for i in s)
pos = b"".join(struct.pack("<3f", *p) for p in positions)
nrm = b"".join(struct.pack("<3f", *n) for n in normals)
uv = b"".join(struct.pack("<2f", *t) for t in uvs)

off_pos = len(idx)
off_nrm = off_pos + len(pos)
off_uv = off_nrm + len(nrm)
# float 접근자는 4바이트 정렬을 요구한다. 인덱스가 uint16 12개 = 24B 라 이미 맞지만,
# 조용히 어긋나면 임포터가 아니라 GPU 에서 터지므로 여기서 단정한다.
assert off_pos % 4 == 0 and off_nrm % 4 == 0 and off_uv % 4 == 0
blob = idx + pos + nrm + uv
(out / "SharedMaterial.bin").write_bytes(blob)

xs = [p[0] for p in positions]
ys = [p[1] for p in positions]
zs = [p[2] for p in positions]

gltf = {
    "asset": {"version": "2.0", "generator": "CreatorEngine PBR-W8 fixture"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "name": "SharedMaterial"}],
    "meshes": [{
        "name": "SharedMaterial",
        # ★ primitive 둘이 **같은 material 0** 을 가리킨다. 이것이 fixture 의 전부다.
        "primitives": [
            {"attributes": {"POSITION": 2, "NORMAL": 3, "TEXCOORD_0": 4},
             "indices": 0, "material": 0},
            {"attributes": {"POSITION": 2, "NORMAL": 3, "TEXCOORD_0": 4},
             "indices": 1, "material": 0},
        ],
    }],
    "materials": [{
        "name": "SharedBase",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.8, 0.8, 0.8, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.5,
        },
    }],
    "accessors": [
        {"bufferView": 0, "byteOffset": 0, "componentType": 5123,
         "count": 6, "type": "SCALAR"},
        {"bufferView": 0, "byteOffset": 12, "componentType": 5123,
         "count": 6, "type": "SCALAR"},
        {"bufferView": 1, "componentType": 5126, "count": len(positions),
         "type": "VEC3", "min": [min(xs), min(ys), min(zs)],
         "max": [max(xs), max(ys), max(zs)]},
        {"bufferView": 2, "componentType": 5126, "count": len(normals),
         "type": "VEC3"},
        {"bufferView": 3, "componentType": 5126, "count": len(uvs),
         "type": "VEC2"},
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": len(idx), "target": 34963},
        {"buffer": 0, "byteOffset": off_pos, "byteLength": len(pos), "target": 34962},
        {"buffer": 0, "byteOffset": off_nrm, "byteLength": len(nrm), "target": 34962},
        {"buffer": 0, "byteOffset": off_uv, "byteLength": len(uv), "target": 34962},
    ],
    "buffers": [{"uri": "SharedMaterial.bin", "byteLength": len(blob)}],
}

(out / "SharedMaterial.gltf").write_text(
    json.dumps(gltf, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

print("bin=%dB offsets=0/%d/%d/%d · primitives=2 · materials=1"
      % (len(blob), off_pos, off_nrm, off_uv))
