"""실제 선언/업로드 본문을 CPU 서비스 경계에서 실행한다. GPU 수용 검사는 아니다."""

import argparse
import hashlib
import os
import re
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


def emit_block(source, path, anchor, semicolon=False):
    text, line = block(source, anchor, semicolon)
    return f'#line {line} "{path.as_posix()}"\n{text}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', choices=('none', 'address', 'thread'), default='none')
    parser.add_argument('--benchmark', action='store_true',
                        help='실제 JobScheduler의 켜짐/꺼짐 조건에서 CPU 모형의 직접 선언 시간을 측정한다.')
    parser.add_argument('--enki-ts-root', type=Path,
                        help='이미 준비된 enkiTS 소스 경로. benchmark에서만 사용하며 다운로드하지 않는다.')
    parser.add_argument('--workers', type=int, nargs='+', choices=(1, 4, 8), default=[4])
    parser.add_argument('--retain-native-storage', action='store_true',
                        help='모형 native storage를 재사용해 교차 스레드 malloc/free 비용을 제외한다.')
    parser.add_argument('--optimization', choices=('1', '2', '3'))
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.source_root.resolve()
    if args.benchmark and (not args.enki_ts_root or args.sanitize != 'none'):
        parser.error('--benchmark requires --enki-ts-root and --sanitize none')
    source_path = root / 'Engine/RenderEngine/GpuGeometryVisibility.cpp'
    header_path = source_path.with_suffix('.h')
    shared_path = source_path.with_name('GpuGeometryVisibilityShared.inl')
    access_path = source_path.with_name('MaterialGraphSceneAccess.h')
    occlusion_path = source_path.with_name('GpuGeometryOcclusion.h')
    production_paths = [source_path, header_path, shared_path, access_path, occlusion_path]
    task_path = root / 'Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h'
    if task_path.is_file():
        production_paths.append(task_path)
    scheduler_sources = []
    if args.benchmark:
        scheduler_sources = [root / 'Engine/Utility_Framework/Concurrency/JobScheduler.cpp',
                             root / 'Engine/Utility_Framework/Concurrency/ThreadPool.cpp',
                             args.enki_ts_root.resolve() / 'src/TaskScheduler.cpp']
        production_paths += scheduler_sources
    hashes = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in production_paths}
    source = source_path.read_text(encoding='utf-8-sig')
    optimized = 'struct DeclarationScratch' in source
    if not optimized and not args.benchmark:
        parser.error('A baseline without retained declaration scratch supports only --benchmark')
    header = header_path.read_text(encoding='utf-8-sig')
    declaration, line = block(header, 'class GpuGeometryVisibility final', True)
    # 본문은 바꾸지 않고 봉인된 테스트 입력을 만드는 friend만 검사 번역 단위에 추가한다.
    declaration = declaration.replace('IRHIUploadTransactionListener\n{',
                                      'IRHIUploadTransactionListener\n{\n    friend struct VisibilityDeclarationProbe;', 1)
    declaration = declaration.replace('class Frame\n    {',
                                      'class Frame\n    {\n        friend struct VisibilityDeclarationProbe;', 1)
    declarations = f'#line {line} "{header_path.as_posix()}"\n{declaration}\n'
    definitions = ''
    for anchor, semicolon in (
        ('constexpr std::uint32_t kVisibilityThreads', False),
    ):
        start = source.index(anchor)
        end = source.index('std::uint32_t GroupMaskWords', start)
        definitions += source[start:end]
    for anchor, semicolon in (
        ('std::uint32_t GroupMaskWords(', False),
        ('std::uint32_t FeedbackWords(', False),
        ('std::uint32_t MeshletDescriptorCount(', False),
        ('std::uint64_t MeshletFeedbackOffset(', False),
        ('std::uint32_t VisibilityGroups(', False),
        ('void DispatchGroups(', False),
        ('struct GeometryVisibilityConstants', True),
        ('bool VisibilityFail(', False),
        ('struct GpuGeometryVisibility::MeshletInputs', True),
        ('struct GpuGeometryVisibility::BufferPool', True),
    ):
        definitions += emit_block(source, source_path, anchor, semicolon)
    definitions += f'#include "{shared_path.as_posix()}"\n'
    access = access_path.read_text(encoding='utf-8-sig')
    access_start = access.index('    inline void NormalizeSceneReads(')
    access_end = access.rfind('} // namespace material_graph')
    definitions += '\nnamespace material_graph\n{\n' + access[access_start:access_end] + '}\n'
    for anchor in (
        'GpuGeometryVisibility::Frame::~Frame()',
        'void GpuGeometryVisibility::Frame::CheckCurrent(',
        'void GpuGeometryVisibility::Frame::DispatchMeshletStage(',
        'void GpuGeometryVisibility::Frame::DeclareMeshletBatch(',
        'RHIBufferSlice GpuGeometryVisibility::Frame::VisibleIds(',
    ):
        definitions += emit_block(source, source_path, anchor)
    probe = Path(__file__).with_name('meshlet_declaration_cpu_probe.cpp').resolve()
    probe_hash = hashlib.sha256(probe.read_bytes()).hexdigest()
    occlusion = occlusion_path.read_text(encoding='utf-8-sig')
    maximum_levels = re.search(r'static constexpr std::uint32_t kMaximumLevels = \d+;', occlusion)
    if not maximum_levels:
        raise ValueError('Missing production HZB level capacity')
    with tempfile.TemporaryDirectory(prefix='creator-meshlet-declaration-') as directory:
        output = Path(directory)
        (output / 'MeshletDeclarationTypes.inl').write_text(declarations, encoding='utf-8')
        (output / 'MeshletDeclarationProduction.inl').write_text(definitions, encoding='utf-8')
        (output / 'MeshletDeclarationConstants.inl').write_text(maximum_levels.group(), encoding='utf-8')
        executable = output / 'meshlet-declaration-cpu'
        optimization = args.optimization or ('2' if args.benchmark else '1')
        command = [args.compiler, '-std=c++23', '-O' + optimization, '-g', '-pthread', '-DCE_CONTRACT_STANDALONE',
                   '-DCE_SHIPPING=1', '-I' + str(output), '-I' + str(root / 'Engine/Utility_Framework'),
                   '-I' + str(root / 'Engine/RenderEngine'), str(probe), '-o', str(executable)]
        if optimized:
            command += ['-DPROBE_OPTIMIZED_DECLARATION=1']
        if args.benchmark:
            (output / 'enkiTS').symlink_to(args.enki_ts_root.resolve() / 'src', target_is_directory=True)
            command += ['-DPROBE_REAL_SCHEDULER=1', *(str(path) for path in scheduler_sources)]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                        '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True, timeout=90)
        if args.benchmark:
            compiler = subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0]
            cpu_max = Path('/sys/fs/cgroup/cpu.max')
            quota = cpu_max.read_text().strip() if cpu_max.is_file() else 'unavailable'
            variant = 'optimized' if optimized else 'baseline'
            print(f'BENCHMARK_ENV compiler={compiler} optimization=O{optimization} '
                  f'affinity={sorted(os.sched_getaffinity(0))} cpu.max={quota} variant={variant}', flush=True)
            for workers in args.workers:
                subprocess.run([str(executable), str(workers), '1' if args.retain_native_storage else '0'],
                               check=True, timeout=40)
        else:
            subprocess.run([str(executable)], check=True, timeout=40)
    for path, digest in hashes.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Production source changed during verification; rerun: {path}')
        label = path.relative_to(root) if path.is_relative_to(root) else path
        print(f'PRODUCTION_SHA256 {label} {digest}')
    if hashlib.sha256(probe.read_bytes()).hexdigest() != probe_hash:
        raise RuntimeError('Probe source changed during verification; rerun')
    print(f'PROBE_SHA256 {probe.name} {probe_hash}')
    if not args.benchmark:
        print('SCOPE exact-production-declaration-and-upload fake-native-device fake-graph '
              'inline no-GPU no-frame-speedup')


if __name__ == '__main__':
    main()
