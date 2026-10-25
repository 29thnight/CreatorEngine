"""메시 선언 준비의 제품 본문을 CPU RHI 경계에서 비교한다. GPU 수용 검사는 아니다."""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


def block(source, anchor, semicolon=False):
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: ''.join('\n' if char == '\n' else ' ' for char in match.group()), source)
    begin = masked.index('{', start)
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == '{') - (masked[index] == '}')
        if depth == 0:
            end = index + 1
            if semicolon:
                if masked[end:].lstrip()[0] != ';':
                    raise ValueError(f'Missing declaration terminator: {anchor}')
                end = masked.index(';', end) + 1
            return source[start:end], source.count('\n', 0, start) + 1
    raise ValueError(f'Unclosed production block: {anchor}')


def emit(source, path, anchor, semicolon=False):
    text, line = block(source, anchor, semicolon)
    return f'#line {line} "{path.as_posix()}"\n{text}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', choices=('none', 'address', 'thread'), default='none')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--enkits', type=Path, help='Use production JobScheduler/ThreadPool with this local enkiTS checkout.')
    parser.add_argument('--workers', type=int, nargs='+', default=[4], help='Native benchmark worker counts, run sequentially.')
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    if args.benchmark and args.sanitize != 'none':
        parser.error('Benchmark release CPU work separately from sanitizer overhead.')
    if any(workers < 1 or workers > 64 for workers in args.workers):
        parser.error('--workers must be between 1 and 64')
    if not args.enkits and args.workers != [4]:
        parser.error('Fake executor uses four workers; use --enkits for native worker-count comparisons.')
    enkits = args.enkits.resolve() if args.enkits else None
    if enkits:
        if (enkits / 'src' / 'TaskScheduler.cpp').is_file():
            enkits = enkits / 'src'
        if not all((enkits / name).is_file() for name in ('TaskScheduler.h', 'TaskScheduler.cpp')):
            parser.error('--enkits must contain TaskScheduler.cpp and TaskScheduler.h, directly or under src')
    root = args.source_root.resolve()
    source_path = root / 'Engine/RenderEngine/MaterialGraphMeshSurface.cpp'
    point_path = source_path.with_name('MaterialGraphSurfaceBatch.h')
    source = source_path.read_text(encoding='utf-8-sig')
    declarations = emit(point_path.read_text(encoding='utf-8-sig'), point_path, 'struct SurfacePoint', True)
    for anchor in ('struct MeshTransformDispatch', 'struct MeshTransformDeclarationScratch',
                   'struct MeshSurfaceBufferPool'):
        declarations += emit(source, source_path, anchor, True)
    definitions = ''
    for anchor in ('bool MeshSurfaceBatch::IsPreparedForGraph(', 'bool MeshSurfaceBatch::PrepareDispatch(',
                   'bool MeshSurfaceBatch::PrepareDeclarationDispatch('):
        definitions += emit(source, source_path, anchor)
    # 제어군은 공개 제품 경로가 아니다. start의 병렬 조건만 false로 바꿔
    # 같은 활성 풀/캐시 교차 순서에서 직접 쓰기와 병렬 실행의 기여를 분리한다.
    control = emit(source, source_path, 'bool MeshSurfaceBatch::PrepareDeclarationDispatch(')
    control = control.replace('MeshSurfaceBatch::PrepareDeclarationDispatch(',
                              'MeshSurfaceBatch::PrepareDeclarationDispatchInlineControl(', 1)
    control, replacements = re.subn(r'(preparation\.start\(\s*slices,\s*)slices\s*>\s*1(\s*,\s*fill\s*\);)',
                                   r'\g<1>false\g<2>', control)
    if replacements != 1:
        raise ValueError('Production admission predicate changed; review the explicit inline control before measuring.')
    definitions += control
    probe = Path(__file__).with_name('mesh_dispatch_declaration_cpu_probe.cpp').resolve()
    with tempfile.TemporaryDirectory(prefix='creator-mesh-dispatch-declaration-') as directory:
        output = Path(directory)
        (output / 'MeshDispatchDeclarationTypes.inl').write_text(declarations, encoding='utf-8')
        (output / 'MeshDispatchDeclarationProduction.inl').write_text(definitions, encoding='utf-8')
        executable = output / 'mesh-dispatch-declaration-cpu'
        command = [args.compiler, '-std=c++23', '-O2', '-g', '-pthread', '-DCE_CONTRACT_STANDALONE',
                   '-DCE_SHIPPING=1', '-Wall', '-Wextra', '-Werror', '-I' + str(output),
                   '-I' + str(root / 'Engine/Utility_Framework'), '-I' + str(root / 'Engine/RenderEngine'),
                   str(probe), '-o', str(executable)]
        if enkits:
            include = output / 'include' / 'enkiTS'
            include.mkdir(parents=True)
            shutil.copyfile(enkits / 'TaskScheduler.h', include / 'TaskScheduler.h')
            command += ['-DCE_MESH_DISPATCH_NATIVE_EXECUTOR=1', '-Wno-unknown-pragmas',
                        '-I' + str(include.parent), str(root / 'Engine/Utility_Framework/Concurrency/JobScheduler.cpp'),
                        str(root / 'Engine/Utility_Framework/Concurrency/ThreadPool.cpp'), str(enkits / 'TaskScheduler.cpp')]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                        '-fno-omit-frame-pointer']
        print('Compiling exact mesh preparation with ' + ('production scheduler/enkiTS' if enkits else 'fake executor'),
              flush=True)
        subprocess.run(command, check=True, timeout=120)
        environment = dict(os.environ)
        for workers in args.workers:
            subprocess.run([str(executable)] + (['--benchmark', str(workers)] if args.benchmark else []),
                           check=True, timeout=90, env=environment)
    paths = (source_path, point_path, source_path.with_name('MeshTransformDeclarationPlan.h'),
             root / 'Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h')
    for path in paths:
        print(f'PRODUCTION_SHA256 {path.relative_to(root)} {hashlib.sha256(path.read_bytes()).hexdigest()}')
    if enkits:
        for path in (root / 'Engine/Utility_Framework/Concurrency/JobScheduler.cpp', root / 'Engine/Utility_Framework/Concurrency/ThreadPool.cpp',
                     enkits / 'TaskScheduler.cpp', enkits / 'TaskScheduler.h'):
            print(f'EXECUTOR_SHA256 {path.name} {hashlib.sha256(path.read_bytes()).hexdigest()}')
    print('SCOPE exact-production-mesh-dispatch-preparation fake-native-device ' +
          ('production-job-scheduler-thread-pool-enkits ' if enkits else 'fake-persistent-job-executor ') +
          'inline-control-start-predicate-only no-GPU no-frame-speedup')


if __name__ == '__main__':
    main()
