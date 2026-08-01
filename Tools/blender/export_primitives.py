# 블렌더에서 기본 도형을 엔진용 모델로 뽑는다.
#
# 왜 필요한가: 지금 저장소에 있는 모델은 실게임 에셋 셋(Gunner_F_Mythic 등)뿐이라
# 렌더 기능을 하나씩 확인하기에 맞지 않다. 캐릭터 메시는 무엇이 틀렸는지 눈으로
# 구분하기 어렵고(노멀이 뒤집혔는지, 접선이 틀렸는지, 그림자가 새는지) 무겁다.
# 기본 도형은 정답을 알고 보는 것이라 어긋난 순간 바로 보인다.
#
# 실행:
#   blender --background --python Tools/blender/export_primitives.py -- --out <디렉터리>
#
# 산출물은 도형당 .glb 하나다. 엔진의 모델 로더가 assimp를 쓰고 저장소의 기존
# 모델도 .glb라, 같은 경로를 타게 하려고 포맷을 맞췄다.

import argparse
import math
import os
import sys

import bpy


# ── 도형 목록 ──
#
# 각각이 무엇을 확인하는 데 쓰이는지 적어 둔다. 목록을 늘릴 때 "왜 이 도형인가"가
# 비어 있으면 검증에 쓰이지 않는 에셋이 쌓인다.
PRIMITIVES = [
    # 이름          설명
    ("Cube",        "면이 평평하고 모서리가 뚜렷하다 — 노멀·접선이 면마다 상수라 어긋나면 바로 보인다"),
    ("Sphere",      "UV 구. 경도/위도 UV라 노멀맵 접선 공간을 확인하기 좋다"),
    ("IcoSphere",   "삼각형이 고른 구. UV 이음매가 없어 셰이딩만 볼 때 쓴다"),
    ("Cylinder",    "평평한 면과 굽은 면이 한 메시에 있다 — 스무딩 그룹 경계 확인"),
    ("Cone",        "한 점으로 모이는 노멀. 정점 노멀 보간이 무너지면 꼭짓점에서 드러난다"),
    ("Torus",       "자기 자신을 가린다 — 자기 그림자(self-shadow)와 여드름 편향 확인용"),
    ("Plane",       "바닥. 그림자가 드리우는 대상이자 캐스케이드 경계가 보이는 면"),
    ("Suzanne",     "오목·볼록이 섞인 실물형 메시. 종합 확인용"),
]


def clear_scene():
    """블렌더 기본 씬(큐브·카메라·조명)을 비운다."""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    # 남은 데이터 블록까지 지운다. 이게 없으면 이름이 Cube.001로 밀린다.
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.objects):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def make_material(name, base_color, metallic, roughness, texture="grid"):
    """PBR 재질 하나. 엔진이 glTF의 metallic/roughness를 읽으므로 그 규격에 맞춘다.

    베이스 컬러 텍스처를 반드시 붙인다. 값(factor)만 넣으면 엔진의 DX11 경로가
    베이스 컬러 SRV를 못 찾아 검게 그린다 — 실측으로 겪었다(dx12.scene은
    1x1 흰색 폴백이 있어 정상이었고, 그래서 '씬은 맞는데 화면만 검은' 상태가
    한동안 구분되지 않았다).

    texture="grid"면 블렌더의 UV 색상 격자를 쓴다. 단색보다 낫다 — UV가
    뒤집혔거나 이음매가 어긋나면 격자가 깨져 바로 보이고, 접선 공간 노멀맵을
    확인할 때도 같은 이미지를 기준으로 삼을 수 있다.
    """
    material = bpy.data.materials.new(name)
    material.use_nodes = True

    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf is None:
        return material

    bsdf.inputs["Base Color"].default_value = base_color
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness

    image_name = name + "_BaseColor"
    if "grid" == texture:
        image = bpy.data.images.new(image_name, width=512, height=512)
        image.generated_type = "COLOR_GRID"
    else:
        image = bpy.data.images.new(image_name, width=16, height=16)
        image.generated_color = base_color

    # GLB 하나로 나가야 하므로 이미지를 파일이 아니라 데이터로 싣는다.
    image.pack()

    texture_node = material.node_tree.nodes.new("ShaderNodeTexImage")
    texture_node.image = image
    material.node_tree.links.new(
        bsdf.inputs["Base Color"], texture_node.outputs["Color"])

    return material


def add_primitive(kind):
    """도형 하나를 만들어 활성 오브젝트로 돌려준다.

    크기를 반지름 0.5 / 한 변 1로 통일한다. 엔진에서 배치할 때 도형마다 다른
    배율을 기억해야 하면 씬 저작이 금방 지저분해진다.
    """
    if kind == "Cube":
        bpy.ops.mesh.primitive_cube_add(size=1.0)
    elif kind == "Sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.5, segments=48, ring_count=24)
        bpy.ops.object.shade_smooth()
    elif kind == "IcoSphere":
        bpy.ops.mesh.primitive_ico_sphere_add(radius=0.5, subdivisions=3)
        bpy.ops.object.shade_smooth()
    elif kind == "Cylinder":
        bpy.ops.mesh.primitive_cylinder_add(radius=0.5, depth=1.0, vertices=48)
        bpy.ops.object.shade_auto_smooth(angle=math.radians(40))
    elif kind == "Cone":
        bpy.ops.mesh.primitive_cone_add(radius1=0.5, depth=1.0, vertices=48)
        bpy.ops.object.shade_auto_smooth(angle=math.radians(40))
    elif kind == "Torus":
        bpy.ops.mesh.primitive_torus_add(major_radius=0.5, minor_radius=0.18,
                                         major_segments=64, minor_segments=24)
        bpy.ops.object.shade_smooth()
    elif kind == "Plane":
        # 바닥은 크게 만든다. 그림자가 드리울 자리가 있어야 하고, 캐스케이드
        # 경계는 넓은 면에서만 보인다.
        bpy.ops.mesh.primitive_plane_add(size=40.0)
    elif kind == "Suzanne":
        bpy.ops.mesh.primitive_monkey_add(size=1.0)
        bpy.ops.object.shade_smooth()
    else:
        raise ValueError("알 수 없는 도형: " + kind)

    return bpy.context.active_object


def unwrap(obj):
    """UV를 편다.

    기본 도형 중 일부는 UV가 없거나 겹쳐 있다. 텍스처·노멀맵 확인에 쓰려면
    쓸 만한 UV가 있어야 하고, 없으면 접선 계산 자체가 성립하지 않는다.
    """
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")


def export_primitive(kind, out_dir):
    clear_scene()

    obj = add_primitive(kind)

    # 오브젝트 이름을 파일명과 맞춘다. 엔진은 모델을 파일 stem으로 키잉하고
    # 씬에 놓을 때는 glTF 노드 이름을 오브젝트 이름으로 쓴다 — 둘이 다르면
    # 배치한 다음 무엇을 가리켜야 하는지가 어긋난다.
    obj.name = "Prim_" + kind
    obj.data.name = "Prim_" + kind

    unwrap(obj)

    # 도형마다 다른 재질 값을 준다. 전부 같은 회색이면 재질 상수가 셰이더에
    # 닿는지 눈으로 구분할 수 없다.
    palette = {
        "Cube":      ((0.80, 0.25, 0.20, 1.0), 0.0, 0.55),
        "Sphere":    ((0.25, 0.55, 0.85, 1.0), 0.0, 0.30),
        "IcoSphere": ((0.30, 0.75, 0.45, 1.0), 0.0, 0.65),
        "Cylinder":  ((0.85, 0.65, 0.20, 1.0), 1.0, 0.35),
        "Cone":      ((0.70, 0.35, 0.75, 1.0), 0.0, 0.80),
        "Torus":     ((0.90, 0.90, 0.90, 1.0), 1.0, 0.15),
        "Plane":     ((0.55, 0.55, 0.58, 1.0), 0.0, 0.90),
        "Suzanne":   ((0.85, 0.55, 0.35, 1.0), 0.0, 0.45),
    }
    color, metallic, roughness = palette[kind]
    obj.data.materials.append(make_material(kind + "_Mat", color, metallic, roughness))

    # 배율·회전을 메시에 굽는다. 남겨 두면 glTF 노드 변환으로 나가고, 엔진에서
    # 배치할 때 저작 의도와 다른 크기가 된다.
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # 이름에 접두사를 붙여 게임 에셋과 섞이지 않게 한다.
    #
    # 하위 폴더로 나누지 않는 이유가 있다 — DataSystem::LoadModel이 모델을
    # Assets/Models 바로 아래로 복사해 거기서 읽는다(경로를 파일명으로 눕힌다).
    # 하위 폴더에 두면 로드 때 복사가 한 번 더 일어나고, 그 복사가 에셋 감시자와
    # 겹쳐 .meta 생성과 경합하다 로드가 실패한다(실측으로 겪었다).
    out_path = os.path.join(out_dir, "Prim_" + kind + ".glb")
    bpy.ops.export_scene.gltf(
        filepath=out_path,
        export_format="GLB",
        use_selection=False,
        export_apply=True,
        # 접선을 같이 내보낸다. 엔진의 GBuffer 패스가 TANGENT/BINORMAL을 읽고,
        # 없으면 접선 공간 노멀맵 경로가 조용히 틀린 결과를 낸다.
        export_tangents=True,
        export_normals=True,
        export_texcoords=True,
        export_yup=True,
    )
    return out_path


def export_material_grid(out_dir):
    """거칠기·금속성 격자를 한 파일로 뽑는다.

    재질 상수가 셰이더에 닿는지 보려면 값이 다른 물체를 나란히 놓아야 하는데,
    엔진 CLI로 재질 값을 바꾸려면 MeshRenderer 안의 Material까지 파고들어야 한다.
    저작 도구에서 값을 넣어 glTF에 실어 보내면 그 경로가 통째로 필요 없어지고,
    덤으로 '한 파일에 여러 재질'이라는 임포트 경로도 같이 확인된다.

    가로 5칸이 거칠기 0.05 → 1.0, 세로 2칸이 금속성 0 / 1이다. 값이 안 닿으면
    격자가 균일해 보이므로 어긋난 순간 눈에 띈다.
    """
    clear_scene()

    columns, rows = 5, 2
    for row in range(rows):
        for col in range(columns):
            bpy.ops.mesh.primitive_uv_sphere_add(
                radius=0.8, segments=48, ring_count=24,
                location=(-4.0 + col * 2.0, row * 2.0, 0.0))
            bpy.ops.object.shade_smooth()

            obj = bpy.context.active_object
            obj.name = "MatGrid_R{}_C{}".format(row, col)
            obj.data.name = obj.name

            roughness = 0.05 + col * (0.95 / (columns - 1))
            metallic = float(row)
            # 여기서는 단색을 쓴다. 격자 무늬가 있으면 거칠기 차이가 무늬에
            # 묻혀 무엇을 보고 있는지 흐려진다.
            obj.data.materials.append(make_material(
                obj.name + "_Mat", (0.72, 0.72, 0.75, 1.0), metallic, roughness,
                texture="solid"))

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    out_path = os.path.join(out_dir, "Prim_MatGrid.glb")
    bpy.ops.export_scene.gltf(
        filepath=out_path,
        export_format="GLB",
        use_selection=False,
        export_apply=True,
        export_tangents=True,
        export_normals=True,
        export_texcoords=True,
        export_yup=True,
    )
    return out_path


def main():
    # 블렌더는 자기 인자를 먼저 먹으므로 '--' 뒤만 우리 것이다.
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []

    parser = argparse.ArgumentParser(description="엔진 검증용 기본 도형을 glb로 뽑는다")
    parser.add_argument("--out", required=True, help="출력 디렉터리")
    args = parser.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)

    print("[도형] 출력 디렉터리: " + args.out)
    for kind, why in PRIMITIVES:
        path = export_primitive(kind, args.out)
        size = os.path.getsize(path)
        print("[도형] {:<10} {:>9,} 바이트  — {}".format("Prim_" + kind, size, why))

    grid = export_material_grid(args.out)
    print("[도형] {:<14} {:>9,} 바이트  — 거칠기 5 x 금속성 2 격자(재질 상수 확인용)".format(
        "Prim_MatGrid", os.path.getsize(grid)))

    print("[도형] 완료 — {}개".format(len(PRIMITIVES) + 1))


if __name__ == "__main__":
    main()
