#!/usr/bin/env python3
"""실제 Scene 준비 본문을 CPU 서비스 경계와 실제 enkiTS 스케줄러로 검증한다.

Windows/RHI 프레임 실행이나 GPU 성능 검사는 아니다. 이미 준비된 enkiTS만 사용한다.
"""

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
BASELINE = '001ff417b7745401622f4f9ab8e8b9b15fb9ab4d'
HOST = Path('Engine/RenderEngine/MaterialGraphSceneHost.cpp')


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
    code, line = block(source, anchor, semicolon)
    return f'#line {line} "{path}"\n{code}\n'


def section(source, path, first, last):
    start = source.index(first)
    end = source.index(last, start)
    line = source.count('\n', 0, start) + 1
    return f'#line {line} "{path}"\n{source[start:end]}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--enkits', required=True, type=Path)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--sanitize', choices=('none', 'address', 'thread'), default='none')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--workers', type=int, nargs='+', choices=(1, 4, 8), default=[4])
    parser.add_argument('--output-executable', type=Path,
                        help='검증한 실행 파일을 보관해 다른 계측 작업과 겹치지 않는 시간에 재실행한다.')
    args = parser.parse_args()
    root = args.source_root.resolve()
    enki = args.enkits.resolve()
    if (enki / 'src' / 'TaskScheduler.cpp').is_file():
        enki = enki / 'src'
    if not all((enki / name).is_file() for name in ('TaskScheduler.cpp', 'TaskScheduler.h')):
        parser.error('--enkits must contain TaskScheduler.cpp and TaskScheduler.h, directly or under src')
    if args.benchmark and args.sanitize != 'none':
        parser.error('benchmark requires --sanitize none')
    header_path = root / HOST.with_suffix('.h')
    light_path = root / 'Engine/RenderEngine/Render/Graph/EnhancedRenderPass.h'
    geometry_path = root / 'Engine/RenderEngine/MaterialGraphFrameGeometry.h'
    records_path = root / 'Engine/RenderEngine/MaterialGraphSceneGpuData.h'
    inputs = [root / HOST, header_path, light_path, geometry_path, records_path,
              root / 'Engine/RenderEngine/MaterialGraphGeometryContract.h',
              root / 'Engine/RenderEngine/MaterialGraphVisibilityContract.h',
              root / 'Engine/RenderEngine/MaterialGraphPreparationTasks.h',
              root / 'Engine/Utility_Framework/JobScheduler.cpp',
              root / 'Engine/Utility_Framework/ThreadPool.cpp', enki / 'TaskScheduler.cpp']
    probe = Path(__file__).with_name('scene_cpu_preparation_probe.cpp')
    snapshots = {path: path.read_bytes() for path in inputs + [probe]}
    hashes = {path: hashlib.sha256(content).hexdigest() for path, content in snapshots.items()}
    source = snapshots[root / HOST].decode('utf-8-sig')
    header = snapshots[header_path].decode('utf-8-sig')
    baseline_bytes = subprocess.check_output(['git', 'show', f'{args.baseline_ref}:{HOST.as_posix()}'], cwd=root)
    baseline = baseline_bytes.decode('utf-8-sig')
    baseline_path = f'{args.baseline_ref}/{HOST.as_posix()}'

    with tempfile.TemporaryDirectory(prefix='creator-scene-preparation-') as directory:
        output = Path(directory)
        def write(name, content):
            (output / name).write_text(content, encoding='utf-8')

        write('SceneForwardDraw.inl', emit(header, header_path, 'struct ForwardDraw', True))
        common = emit(snapshots[light_path].decode('utf-8-sig'), light_path, 'struct EnhancedLight', True)
        common += emit(source, root / HOST, 'struct SceneConstants', True)
        write('SceneConstants.inl', common)
        definitions = 'namespace baseline\n{\nstruct SceneHost { using ForwardDraw = ::ForwardDraw; };\n'
        definitions += emit(baseline, baseline_path, 'struct SceneCpuPreparation final', True) + '}\n'
        definitions += ('namespace optimized\n{\nstruct SceneHost { using ForwardDraw = ::ForwardDraw; '
                        'struct PreparationScratch; };\n')
        definitions += emit(source, root / HOST, 'struct SceneCpuPreparation final', True)
        definitions += emit(source, root / HOST, 'struct SceneHost::PreparationScratch', True) + '}\n'
        write('SceneCpuPreparation.inl', definitions)
        write('SceneGeometryRecord.inl', emit(snapshots[geometry_path].decode('utf-8-sig'), geometry_path,
                                              'struct alignas(16) Record', True))
        write('SceneGpuRecord.inl', emit(snapshots[records_path].decode('utf-8-sig'), records_path,
                                       'struct alignas(16) Record', True))
        write('SceneFrameDraw.inl', emit(source, root / HOST, 'struct Draw\n', True))
        write('SceneUsesMeshlets.inl', emit(source, root / HOST, 'bool UsesMeshlets(const Draw& draw) const'))
        write('ScenePrepareGuard.inl', section(source, root / HOST,
                                              '        if (m_preparing.test_and_set(',
                                              '        ce::profile_scope profile{ce::marker<"MaterialGraphScenePrepare">()};'))
        initial_start = source.index('std::vector<std::shared_ptr<const MeshSurfaceBatch>> shadowMeshletGeometry;')
        initial = source[initial_start:]
        initial_loop, _ = block(initial, 'for (const auto& draw : candidate->input->Draws())')
        initial_line = source.count('\n', 0, source.index(initial_loop, initial_start)) + 1
        write('SceneInitialGeometry.inl', f'#line {initial_line} "{root / HOST}"\n{initial_loop}\n')
        write('SceneCameraPipeline.inl', section(source, root / HOST,
                                                 '        nextPreparationPhase(ce::marker<"MaterialSceneCameraPrepare">());',
                                                 '        nextPreparationPhase(ce::marker<"MaterialSceneIndirectCommandsPrepare">());'))
        baseline_draws = section(baseline, baseline_path,
                                '        nextPreparationPhase(ce::marker<"MaterialSceneDrawsPrepare">());',
                                '        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> previousCameraHistory;')
        write('SceneBaselineDraws.inl', baseline_draws)
        include = output / 'enkiTS'
        include.mkdir()
        shutil.copyfile(enki / 'TaskScheduler.h', include / 'TaskScheduler.h')
        executable = output / 'scene-cpu-preparation'
        command = [args.compiler, '-std=c++23', '-O2', '-g', '-pthread', '-DCE_CONTRACT_STANDALONE=1',
                   '-DCE_SHIPPING=1', '-I', str(output), '-I', str(root),
                   str(probe), str(root / 'Engine/Utility_Framework/JobScheduler.cpp'),
                   str(root / 'Engine/Utility_Framework/ThreadPool.cpp'), str(enki / 'TaskScheduler.cpp'),
                   '-o', str(executable)]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                        '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        print('Compiling exact Scene CPU metadata/camera pipeline + real JobScheduler/ThreadPool/enkiTS', flush=True)
        subprocess.run(command, check=True, timeout=120)
        if args.output_executable:
            saved = args.output_executable.resolve()
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(executable, saved)
            saved.chmod(executable.stat().st_mode)
        if args.benchmark:
            for workers in args.workers:
                subprocess.run([str(executable), '--benchmark', str(workers)], check=True, timeout=45)
        else:
            subprocess.run([str(executable)], check=True, timeout=45)
    for path, digest in hashes.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Production/probe source changed during verification; rerun: {path}')
        label = path.relative_to(root) if path.is_relative_to(root) else path
        print(f'SOURCE_SHA256 {label} {digest}')
    print(f'BASELINE {args.baseline_ref} host-sha256={hashlib.sha256(baseline_bytes).hexdigest()}')
    print('SCOPE exact-production-CPU-metadata contiguous-camera-dispatch-draws-publication-joins '
          'real-engine-scheduler fake-sealed-input fake-RHI no-GPU no-frame-speedup')


if __name__ == '__main__':
    main()
