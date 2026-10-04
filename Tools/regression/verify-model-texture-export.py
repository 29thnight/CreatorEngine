"""Native JPG/PNG export and ambiguous texture identity regression."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import yaml

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--project", required=True, type=Path)
p.add_argument("--cooker", required=True, type=Path)
p.add_argument("--work", required=True, type=Path)
args = p.parse_args()
project = args.project.resolve()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=False)
generations = project / "Library/ModelAssetGenerations"
cases = {}
for model in sorted((project / "Assets").rglob("*")):
    if model.suffix.lower() not in {".glb", ".fbx", ".gltf"}:
        continue
    meta = yaml.safe_load(Path(str(model) + ".meta").read_bytes())
    if not meta.get("assetId"):
        continue
    folder = generations / meta["assetId"] / str(meta["generation"])
    for extension in (".jpg", ".png"):
        if extension not in cases and list((folder / "textures").glob("*" + extension)):
            cases[extension] = (model, folder, meta)
if len(cases) != 2:
    raise ValueError("JPG and PNG generation fixtures required")

def run(model, root, output):
    result = subprocess.run([str(args.cooker.resolve()), "--asset-root", str(project / "Assets"),
                             "--output", str(output), "--generation-root", str(root), "--model", str(model)],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    output.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
    return result

payloads = 0
for extension, (model, folder, meta) in cases.items():
    output = work / extension[1:]
    result = run(model, generations, output)
    if result.returncode:
        raise ValueError(result.stdout + result.stderr)
    exported = output / "Derived/Models" / meta["assetId"][:2] / meta["assetId"] / str(meta["generation"]) / "textures"
    for original in (folder / "textures").iterdir():
        if hashlib.sha256(original.read_bytes()).digest() != hashlib.sha256((exported / original.name).read_bytes()).digest():
            raise ValueError("Texture bytes or extension changed during export")
        payloads += 1

model, folder, meta = cases[".jpg"]
negative_root = work / "ambiguous-generations"
negative = negative_root / meta["assetId"] / str(meta["generation"])
shutil.copytree(folder, negative)
original = next((negative / "textures").glob("*.jpg"))
shutil.copyfile(original, original.with_suffix(".png"))
result = run(model, negative_root, work / "ambiguous")
if result.returncode == 0 or "ambiguous payload files" not in result.stdout + result.stderr:
    raise ValueError("Ambiguous texture identity was accepted")
receipt = dict(result="MODEL_TEXTURE_EXPORT_OK", payloads=payloads, jpg=True, png=True,
               exactBytes=True, ambiguousRejected=True)
(work / "result.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(json.dumps(receipt))
