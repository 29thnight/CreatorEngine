"""Render-free product acceptance: inspect structured reports, never log markers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

BLOCKED = re.compile(r'(?:dxgi|d3d\d*|d3dcompiler.*|dxcompiler|vulkan.*|slang.*|physxgpu.*|physxdevice.*|ryml|c4core|renderengine|creatoreditor|editor)\.(?:dll|lib)$', re.I)
NS = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_boundary(root, overrides=None):
    overrides = overrides or {}
    def read(path):
        return overrides.get(path, path.read_text(encoding="utf-8-sig"))
    visited, projects = set(), set()
    include = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)

    def source(path):
        path = path.resolve()
        if not path.is_relative_to(root) or not path.is_file():
            return
        visited.add(path)
        relative = path.relative_to(root).as_posix()
        text = read(path)
        # Shared Scene headers describe optional presentation types too. Audit
        # host adapters strictly; audit the final linked closure separately.
        if relative.startswith(('HeadlessHost/', 'Engine/SceneRuntime/Headless/')):
            require(not re.search(r'#\s*include\s*[<"](?:ryml|c4/|d3d|dxgi|vulkan)', text, re.I),
                    'HEADLESS_BACKEND_INCLUDE: ' + relative)
            require(not re.search(r'#\s*include\s*"[^"]*(?:RenderEngine/|RenderScene\.h|AuthoringParsedDocument\.h)', text),
                    'HEADLESS_SERVICE_INCLUDE: ' + relative)
            require(not re.search(r'struct\s+Entity\b|std::vector<Entity>\s+entities_', text),
                    'HEADLESS_DUPLICATE_ENTITY_STORE: ' + relative)
        for child in include.findall(text):
            candidate = (path.parent / child).resolve()
            if candidate.is_file() and candidate not in visited:
                # Record source fingerprints without treating declarations as linked code.
                if candidate.relative_to(root).as_posix().startswith(('HeadlessHost/', 'Engine/SceneRuntime/Headless/')):
                    source(candidate)
                else:
                    visited.add(candidate)

    def project(path):
        path = path.resolve()
        if path in projects:
            return
        projects.add(path)
        allowed = {'HeadlessHost', 'SceneRuntimeHeadless', 'EngineContracts', 'GCCE', 'Utility_Framework'}
        require(path.stem in allowed, 'HEADLESS_PROJECT_DEPENDENCY: ' + str(path))
        xml = ET.ElementTree(ET.fromstring(read(path)))
        for item in xml.findall('.//m:AdditionalDependencies', NS):
            require(not any(BLOCKED.search(part.strip()) for part in (item.text or '').split(';')),
                    'HEADLESS_LINK_DEPENDENCY: ' + str(path))
        if path.stem in ('HeadlessHost', 'SceneRuntimeHeadless'):
            require('CE_SCENE_HEADLESS=1' in read(path), 'HEADLESS_HOST_CAPABILITY_MISSING')
            for item in xml.findall('.//m:ClCompile', NS):
                if 'Include' in item.attrib:
                    source(path.parent / item.attrib['Include'].replace('\\', '/'))
        for item in xml.findall('.//m:ProjectReference', NS):
            project(path.parent / item.attrib['Include'].replace('\\', '/'))

    project(root / 'HeadlessHost/HeadlessHost.vcxproj')
    for name in ('Scene.cpp', 'Entity.cpp', 'Component.cpp', 'Transform.cpp', 'SceneManager.cpp',
                 'ScriptComponent.cpp', 'ScriptObjectRegistry.cpp', 'ClrHost.cpp'):
        require(root / 'Engine/SceneRuntime' / name in visited, 'SHARED_SCENE_SOURCE_MISSING: ' + name)
    require(not (root / 'Engine/SceneRuntime/Headless/HeadlessBindings.h').exists(), 'SECOND_MANAGED_ABI_REINTRODUCED')
    return {'result': 'HEADLESS_SOURCE_BOUNDARY_OK', 'projects': len(projects),
            'files': {p.relative_to(root).as_posix(): digest(p) for p in sorted(visited | projects)}}


def inventory(package):
    manifest = json.loads((package / 'package.json').read_text(encoding='utf-8-sig'))
    declared = {item['path']: item for item in manifest['files']}
    actual = {p.relative_to(package).as_posix(): p for p in package.rglob('*') if p.is_file() and p.name != 'package.json'}
    require(set(declared) == set(actual), 'Headless package inventory mismatch')
    for name, path in actual.items():
        require(not BLOCKED.search(path.name), 'Forbidden packaged dependency: ' + name)
        require(declared[name]['sha256'].lower() == digest(path), 'Headless package fingerprint mismatch: ' + name)
    return manifest


def product(root, package, cooker, output, imports, link_map):
    work = output.parent
    manifest = inventory(package)
    import_text = imports.read_text(encoding='utf-8-sig')
    require(not any(BLOCKED.search(name) for name in re.findall(r'[\w.-]+\.dll', import_text, re.I)), 'Forbidden native import')
    map_text = link_map.read_text(encoding='utf-8-sig')
    require(not re.search(r'ryml|c4core|RenderEngine\.lib|AuthoringParsedDocument|EnhancedSceneRenderer|ImGui|d3d12\.lib|dxgi\.lib', map_text, re.I), 'Forbidden static link member')
    results = []
    env = {**os.environ, 'CE_PHYSICS_RESOURCE_PROBE': '1'}
    # No runtime dependency may be satisfied by an Editor/SDK PATH entry.
    env['PATH'] = os.pathsep.join([str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']])

    def run(name, scene=None, frames=120, delta=1 / 60, success=True, inspect=True, relative_managed=False, transfer=0):
        report = work / (name + '.json')
        arguments = [str(package / 'HeadlessHost.exe'), '--scene', str(scene or package / 'Scene.cedo'),
                     '--managed', 'Managed' if relative_managed else str(package / 'Managed'), '--report', str(report),
                     '--frames', str(frames), '--frame-seconds', str(delta)]
        if inspect:
            arguments += ['--inspect-type', 'CreatorEngine.Scripts.TransferProbe' if transfer else 'CreatorEngine.Scripts.HeadlessTickProbe', '--inspect-method', 'State']
        if transfer:
            arguments += ['--transfer-at-frame', str(transfer)]
        with (work / (name + '.log')).open('wb') as log:
            process = subprocess.run(arguments, cwd=package, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=60)
        data = json.loads(report.read_text(encoding='utf-8-sig'))
        require((process.returncode == 0) == success, name + ': unexpected exit: ' + str(data.get('error', data)))
        require(data['exitCode'] == process.returncode, name + ': report exit mismatch')
        require(data['scene']['stopped'], name + ': owners were not retired')
        require(data['scene']['implementation'] == 'Scene/Entity/ScriptComponent/SceneManager', name + ': wrong runtime implementation')
        require(data['scene']['liveObjects'] == data['scene']['roots'] == data['scene']['liveScriptHandles'] == 0, name + ': graph or script handle leak')
        require(data['physicsResources']['enabled'] and data['physicsResources']['balanced'], name + ': physics owner leak')
        require(data['observations']['created'] == data['observations']['destroyed'], name + ': lifetime notification imbalance')
        require(data['observations']['invalidWrites'] == 0, name + ': observer identity was lost')
        require(not any(BLOCKED.search(Path(path).name) for path in data['modules']), name + ': forbidden loaded module')
        if success:
            require(data['result'] == 'HEADLESS_SCENE_OK' and data['scene']['cpuPhysics'], name + ': CPU runtime unavailable')
            require(not data['scene']['managedErrors'], name + ': managed error logged')
            require(data['scene']['engineFrameCount'] == data['scene']['frames'], name + ': common frame clock was not advanced')
            require(data['observations']['physicsWrites'] > 0, name + ': physics write source was lost')
            if not transfer:
                require(data['observations']['scriptWrites'] > 0, name + ': script write source was lost')
        results.append({'name': name, 'exitCode': process.returncode, 'reportSha256': digest(report)})
        return data

    character_positions = []
    for rate in (30, 60, 144):
        data = run('cadence-' + str(rate), frames=rate * 2, delta=1 / rate)
        scene = data['scene']
        state = json.loads(data['scriptState'])
        require(scene['committedTicks'] == scene['managedBatches'] == scene['physicsSteps'] == state['ticks'] == 120,
                'Fixed tick / F# / physics count mismatch')
        require(state['initialized'] == state['began'] == state['ended'] == state['uninitialized'] == state['cleanup'] == 1,
                'F# lifecycle cleanup missing or duplicated')
        require(state['failures'] == 0 and state['stale'] == 2 and state['snapshots'] == 1, 'F# boundary assertions incomplete')
        require(state['disabledInitialized'] == state['disabledRetired'] == 0 and state['disabledEnableCalls'] == state['disabledUpdates'] == 0, 'Authored disabled lifecycle diverged')
        entities = {item['name']: item for item in scene['entities']}
        require(abs(entities['Mover']['x'] - 6) < 0.001 and abs(entities['Falling']['y'] - 0.5) < 0.005,
                'Actual CPU trajectory or floor contact diverged')
        character_positions.append([entities['Character'][axis] for axis in ('x', 'y', 'z')])
        require(entities['Character']['x'] > -8 and 0.9 < entities['Character']['y'] < 1.2, 'Actual CCT did not move/land')
        require(scene['zeroTickFrames'] > 0 if rate == 144 else scene['multiTickFrames'] > 0 if rate == 30 else scene['zeroTickFrames'] == scene['multiTickFrames'] == 0,
                'Zero/multi tick cadence was not exercised')

    require(max(abs(a - b) for row in character_positions for a, b in zip(row, character_positions[0])) < 1e-5, 'CCT cadence trajectories diverged')

    # A real monotonic-clock run supplements the synthetic cadence matrix.
    data = run('real-clock', frames=250, delta=0)
    require(not data['syntheticFrameDelta'] and data['scene']['committedTicks'] > 3,
            'Steady-clock host did not advance')
    require(data['scene']['committedTicks'] == json.loads(data['scriptState'])['ticks'] == data['scene']['physicsSteps'],
            'Real-clock managed/physics ticks diverged')

    unicode_scene = work / '한글 씬.cedo'
    unicode_scene.write_bytes((package / 'Scene.cedo').read_bytes())
    data = run('unicode-and-relative-path', unicode_scene, relative_managed=True)
    require(json.loads(data['scriptState'])['ticks'] == 120, 'Unicode path did not execute the authored scene')

    # Corrupt inputs never enter CLR or commit a tick.
    original = (package / 'Scene.cedo').read_bytes()
    for name, content in [('authoring-text', (root / 'Tools/regression/fixtures/headless/Scene.creator').read_bytes()),
                          ('truncated', original[:-1]), ('wrong-version', original[:4] + b'\x7f\0' + original[6:])]:
        path = work / (name + '.cedo')
        path.write_bytes(content)
        data = run(name, path, success=False, inspect=False)
        require(data['scene']['committedTicks'] == 0, name + ': malformed input advanced')

    source = (root / 'Tools/regression/fixtures/headless/Scene.creator').read_text(encoding='utf-8-sig')
    sparse = re.sub(r'(?m)^(    m_index: )(\d+)$', lambda m: m[1] + str(int(m[2]) * 10), source)
    sparse = sparse.replace('m_parentIndex: -1', 'm_parentIndex: 4294967295')
    sparse = sparse.replace('m_rootIndex: 0', 'm_rootIndex: 4294967295')
    sparse = sparse.replace('m_childrenIndices: [1, 2, 3, 4, 5, 6]', 'm_childrenIndices: [10, 20, 30, 50, 60]')
    retiring_start = sparse.index('    m_name: Retiring')
    retiring_end = sparse.index('  - Entity:', retiring_start)
    block = sparse[retiring_start:retiring_end].replace('m_parentIndex: 0', 'm_parentIndex: 50').replace('x: -10,', 'x: -20,')
    sparse = sparse[:retiring_start] + block + sparse[retiring_end:]
    legacy = source.replace('''    m_components:
      - Transform: 13979161094353040123
        m_instanceID: 1005
      - ScriptComponent: 1''', '''    m_transform:
      position: {x: 5, y: 0, z: 0}
    m_components:
      - ScriptComponent: 1''')
    legacy = legacy.replace('"1|3|Speed"', '"1|3|Speed", "1|5|Expected Command Initial X"')
    for name, text in [('sparse-forward-hierarchy', sparse), ('legacy-transform', legacy)]:
        assets = work / name / 'Assets'
        assets.mkdir(parents=True)
        path = assets / 'Scene.creator'
        path.write_text(text, encoding='utf-8')
        path.with_suffix('.creator.meta').write_text('guid: 6e1e1f00-a8c1-4c91-8900-53be5211a102\n', encoding='utf-8')
        cooked = work / name / 'Cooked'
        with (work / (name + '-cook.log')).open('wb') as log:
            process = subprocess.run([str(cooker), '--asset-root', str(assets), '--scene', str(path), '--output', str(cooked)], stdout=log, stderr=subprocess.STDOUT, timeout=120)
        require(process.returncode == 0, name + ': actual cook failed')
        paths = list((cooked / 'Derived/Scenes').rglob('*.creator'))
        require(len(paths) == 1, name + ': cooked scene missing')
        data = run(name, paths[0])
        require(json.loads(data['scriptState'])['failures'] == 0 and data['scene']['committedTicks'] == 120,
                name + ': existing authoring/hierarchy contract changed')

    mutations = {
        'presentation-component': source.replace('ScriptComponent: 1', 'MeshRenderer: 1'),
        'hierarchy-cycle': source.replace('m_parentIndex: -1', 'm_parentIndex: 2'),
        'unknown-body-field': source.replace('m_motion: 2', 'm_unknownPhysicsOption: 1\n        m_motion: 2', 1),
        'external-geometry': source.replace('kind: 0', 'kind: 3', 1),
        'field-mismatch': source.replace('1|3|Speed', '2|3|Speed'),
        'tick-exception': source.replace('"1|3|Speed"', '"1|3|Speed", "2|10|Fail At Tick"'),
        'end-exception': source.replace('"1|3|Speed"', '"1|3|Speed", "3|1|Fail At End"'),
    }
    for name, text in mutations.items():
        assets = work / name / 'Assets'
        assets.mkdir(parents=True)
        path = assets / 'Scene.creator'
        path.write_text(text, encoding='utf-8')
        path.with_suffix('.creator.meta').write_text('guid: 6e1e1f00-a8c1-4c91-8900-53be5211a100\n', encoding='utf-8')
        cooked = work / name / 'Cooked'
        with (work / (name + '-cook.log')).open('wb') as log:
            process = subprocess.run([str(cooker), '--asset-root', str(assets), '--scene', str(path), '--output', str(cooked)], stdout=log, stderr=subprocess.STDOUT, timeout=120)
        require(process.returncode == 0, name + ': actual cooker failed')
        cooked_paths = list((cooked / 'Derived/Scenes').rglob('*.creator'))
        require(len(cooked_paths) == 1, name + ': missing cooked mutation')
        inspect = name in ('tick-exception', 'end-exception')
        data = run(name, cooked_paths[0], success=False, inspect=inspect)
        if inspect:
            state = json.loads(data['scriptState'])
            require(state['ended'] == state['uninitialized'] == state['cleanup'] == 1, name + ': teardown stopped on error')
            require(data['scene']['committedTicks'] == (10 if name == 'tick-exception' else 120), name + ': isolated script fault changed the ordinary tick contract')
        else:
            require(data['scene']['committedTicks'] == 0, name + ': invalid cooked input was admitted')

    # Exercise the existing SceneManager DDOL path with actual native owners.
    assets = work / 'ddol-transfer' / 'Assets'
    assets.mkdir(parents=True)
    path = assets / 'Scene.creator'
    path.write_text(source.replace('m_scriptType: HeadlessTickProbe', 'm_scriptType: TransferProbe')
                    .replace('m_fieldData: ["1|3|Speed"]', 'm_fieldData: []'), encoding='utf-8')
    path.with_suffix('.creator.meta').write_text('guid: 6e1e1f00-a8c1-4c91-8900-53be5211a101\n', encoding='utf-8')
    cooked = work / 'ddol-transfer' / 'Cooked'
    with (work / 'ddol-transfer-cook.log').open('wb') as log:
        process = subprocess.run([str(cooker), '--asset-root', str(assets), '--scene', str(path), '--output', str(cooked)], stdout=log, stderr=subprocess.STDOUT, timeout=120)
    require(process.returncode == 0, 'DDOL fixture cook failed')
    paths = list((cooked / 'Derived/Scenes').rglob('*.creator'))
    require(len(paths) == 1, 'DDOL cooked scene missing')
    data = run('ddol-transfer', paths[0], transfer=30)
    state = json.loads(data['scriptState'])
    require(data['scene']['ddolTransferred'], 'Native DDOL identity/state assertions did not execute')
    require(state['ticks'] == 120 and state['initialized'] == state['began'] == state['ended'] == state['retired'] == 1,
            'DDOL reconstructed the script or duplicated simulation hooks')
    require(state['added'] == state['removing'] == 2 and state['failures'] == 0, 'DDOL owner handle/hook mismatch')

    script_dir = 'Managed/FSharpNative/Scripts' if (package / 'Managed/CreatorEngine.NativeBindings.dll').is_file() else 'Managed/Scripts'
    missing = package / script_dir / 'GameScripts.dll'
    hidden = missing.with_suffix('.held')
    missing.rename(hidden)
    try:
        data = run('missing-scripts', success=False, inspect=False)
        require(data['scene']['committedTicks'] == 0, 'Missing scripts advanced')
    finally:
        hidden.rename(missing)

    # A package canary must fail even before launching a process.
    forbidden = package / 'dxgi.dll'
    forbidden.write_bytes(b'boundary canary')
    rejected = False
    try:
        inventory(package)
    except ValueError:
        rejected = True
    finally:
        forbidden.unlink()
    require(rejected, 'Forbidden package canary was accepted')
    inventory(package)
    return {'result': 'HEADLESS_SCENE_ACCEPTANCE_OK', 'scope': 'Shared Scene/Entity renderless execution; synthetic frame input, not GPU/FPS/transport acceptance',
            'runs': results, 'package': manifest, 'source': source_boundary(root),
            'link': {'importsSha256': digest(imports), 'mapSha256': digest(link_map), 'forbiddenMembers': 0}}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--source-only', action='store_true')
    parser.add_argument('--package', type=Path)
    parser.add_argument('--cooker', type=Path)
    parser.add_argument('--imports', type=Path)
    parser.add_argument('--map', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    result = source_boundary(root) if args.source_only else product(root, args.package.resolve(), args.cooker.resolve(), args.output.resolve(), args.imports.resolve(), args.map.resolve())
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(result['result'], 'evidence=' + str(args.output))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError, KeyError) as error:
        print('HEADLESS_SCENE_FAILED:', str(error), file=sys.stderr)
        sys.exit(1)
