"""Dependency regressions use in-memory mutations; the checkout is never changed."""
import argparse
import json
from pathlib import Path
from headless_scene import source_boundary, require, BLOCKED

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
source_boundary(root)
header = root / 'Engine/SceneRuntime/Headless/HeadlessScene.h'
project = root / 'HeadlessHost/HeadlessHost.vcxproj'
cases = [
    (header, header.read_text(encoding='utf-8-sig') + '\n#include "../../RenderEngine/RenderScene.h"\n'),
    (header, header.read_text(encoding='utf-8-sig') + '\n#include "../../Utility_Framework/AuthoringParsedDocument.h"\n'),
    (header, header.read_text(encoding='utf-8-sig') + '\n#include <ryml/ryml.hpp>\n'),
    (project, project.read_text(encoding='utf-8-sig').replace('nethost.lib;', 'dxgi.lib;nethost.lib;')),
    (project, project.read_text(encoding='utf-8-sig').replace('..\\Engine\\Contracts\\EngineContracts.vcxproj', '..\\Engine\\SceneRuntime\\SceneRuntime.vcxproj')),
]
for path, text in cases:
    rejected = False
    try:
        source_boundary(root, {path: text})
    except ValueError:
        rejected = True
    require(rejected, 'Headless boundary mutation accepted: ' + str(path))
for name in ('dxgi.dll', 'd3d12.dll', 'PhysXGpu_64.dll', 'PhysXDevice64.dll', 'RenderEngine.lib', 'ryml.lib', 'slang.dll'):
    require(BLOCKED.search(name), 'Headless module deny rule missing: ' + name)
args.output.write_text(json.dumps({'result': 'HEADLESS_BOUNDARY_CANARIES_OK', 'mutations': len(cases), 'modules': 7}, indent=2), encoding='utf-8')
print('HEADLESS_BOUNDARY_CANARIES_OK mutations=5 modules=7')
