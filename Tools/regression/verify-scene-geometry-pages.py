"""ScenePrepare의 실제 geometry 페이지 배치 본문을 기준 커밋과 비교한다. GPU 검사는 아니다."""

import argparse
import hashlib
import os
import re
import subprocess
import tempfile
from pathlib import Path


BASELINE = '001ff417b7745401622f4f9ab8e8b9b15fb9ab4d'
SOURCE = Path('Engine/RenderEngine/MaterialGraphSceneGpuData.h')


def block(source, anchor):
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: ''.join('\n' if char == '\n' else ' ' for char in match.group()), source)
    begin = masked.index('{', start)
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == '{') - (masked[index] == '}')
        if depth == 0:
            return source[start:index + 1], source.count('\n', 0, start) + 1
    raise ValueError(f'Unclosed production block: {anchor}')


def counted(body):
    # 이 복사본은 작업량 계수 전용이다. 검증·시간 측정은 수정하지 않은 제품 본문을 쓴다.
    for name in ('findVertex', 'findTopology'):
        pattern = r'(const auto ' + name + r'\s*=\s*\[[^\]]*\]\s*\([^)]*\)\s*\{)'
        counter = 'vertexSearches' if name == 'findVertex' else 'topologySearches'
        body, count = re.subn(pattern, r'\g<1> ++Probe::' + counter + ';', body)
        if count != 1:
            raise ValueError(f'Review operation-counter instrumentation for {name}')
    for expression, counter in (('page.vertices[i].buffer == batch.Buffer()', 'vertexComparisons'),
                                ('value.buffer == meshlets.buffer', 'topologyComparisons')):
        if body.count(expression) != 1:
            raise ValueError(f'Review comparison instrumentation: {expression}')
        body = body.replace(expression, '(++Probe::' + counter + ', ' + expression + ')', 1)
    return body


def extract(source, label, namespace, count=False):
    start = source.index('struct SceneGpuData final')
    end = source.index('bool AppendMaterial(', start)
    prefix = source[start:end]
    method, line = block(source, 'bool AssignGeometry(')
    bindings, binding_line = block(source, 'bool PrepareBindings(')
    if count:
        method = counted(method)
    return (f'namespace {namespace}\n{{\n#line {source.count(chr(10), 0, start) + 1} "{label}"\n'
            + prefix + f'\n#line {line} "{label}"\n' + method
            + f'\n#line {binding_line} "{label}"\n' + bindings + '\n};\n}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--baseline-ref', default=BASELINE)
    args = parser.parse_args()
    if args.sanitize and args.benchmark:
        parser.error('Run release timing separately from sanitizer validation.')
    root = args.source_root.resolve()
    path = root / SOURCE
    source_bytes = path.read_bytes()
    source = source_bytes.decode('utf-8-sig')
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    baseline = subprocess.check_output(['git', 'show', f'{args.baseline_ref}:{SOURCE.as_posix()}'],
                                       cwd=root, text=True)
    label = SOURCE.as_posix()
    generated = extract(baseline, label + '@' + args.baseline_ref, 'Baseline')
    generated += extract(source, str(path), 'Prepared')
    generated += extract(baseline, label + '@' + args.baseline_ref, 'BaselineCounted', True)
    generated += extract(source, str(path), 'PreparedCounted', True)
    probe = Path(__file__).with_name('scene_geometry_pages_cpu_probe.cpp').resolve()
    probe_hash = hashlib.sha256(probe.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='creator-scene-geometry-pages-') as directory:
        output = Path(directory)
        (output / 'SceneGeometryPagesProduction.inl').write_text(generated, encoding='utf-8')
        executable = output / 'scene-geometry-pages-cpu'
        command = [args.compiler, '-std=c++23', '-O2', '-g', '-DCE_CONTRACT_STANDALONE=1',
                   '-Wall', '-Wextra', '-Werror', '-I' + str(output),
                   '-I' + str(root / 'Engine/RenderEngine'), str(probe), '-o', str(executable)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-fno-sanitize-recover=all']
        print('Compiling exact SceneGpuData::AssignGeometry and PrepareBindings, baseline and prepared.', flush=True)
        subprocess.run(command, check=True, timeout=120)
        subprocess.run([str(executable)] + (['--benchmark'] if args.benchmark else []),
                       check=True, timeout=120, env=dict(os.environ))
    if hashlib.sha256(path.read_bytes()).hexdigest() != source_hash:
        raise RuntimeError('Production source changed during verification; rerun the probe.')
    if hashlib.sha256(probe.read_bytes()).hexdigest() != probe_hash:
        raise RuntimeError('Probe source changed during verification; rerun the probe.')
    print(f'BASELINE_REF {args.baseline_ref}')
    print(f'BASELINE_SHA256 {hashlib.sha256(baseline.encode()).hexdigest()}')
    print(f'PRODUCTION_SHA256 {SOURCE.as_posix()} {source_hash}')
    print(f'PROBE_SHA256 {probe.name} {probe_hash}')
    print('SCOPE exact-production-scene-geometry-pages actual-RHI-value-types fake-mesh-device '
          'counter-copies-separate-from-timing no-GPU no-frame-speedup')


if __name__ == '__main__':
    main()
