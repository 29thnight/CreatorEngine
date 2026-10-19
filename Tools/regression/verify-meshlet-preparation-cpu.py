"""기준/변경의 실제 메시릿 준비를 CPU RHI 경계에서 비교한다. GPU 검사는 아니다."""

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
                end = masked.index(';', end) + 1
            return source[start:end], source.count('\n', 0, start) + 1
    raise ValueError(f'Unclosed production block: {anchor}')


def emit_block(source, path, anchor, semicolon=False):
    text, line = block(source, anchor, semicolon)
    return f'#line {line} "{path}"\n{text}\n'


def variant(source, header, label):
    declaration, line = block(header, 'class GpuGeometryVisibility final', True)
    begin = declaration.index('{') + 1
    declaration = declaration[:begin] + '\n    template<class> friend struct ::PreparationAccess;' + declaration[begin:]
    begin = declaration.index('{', declaration.index('class Frame')) + 1
    declaration = declaration[:begin] + '\n        template<class> friend struct ::PreparationAccess;' + declaration[begin:]
    result = ''
    result += f'#line {line} "{label}/GpuGeometryVisibility.h"\n{declaration}\n'
    begin = source.index('constexpr std::uint32_t kVisibilityThreads')
    end = source.index('std::uint32_t GroupMaskWords(', begin)
    result += source[begin:end]
    for anchor, semicolon in (
        ('std::uint32_t GroupMaskWords(', False),
        ('std::uint32_t FeedbackWords(', False),
        ('std::uint64_t MeshletOutputWords(', False),
        ('std::uint64_t MeshletFeedbackOffset(', False),
        ('struct GeometryVisibilityConstants', True),
        ('bool VisibilityFail(', False),
        ('GeometryVisibilityConstants VisibilityConstants(', False),
        ('struct GpuGeometryVisibility::MeshletInputs', True),
        ('struct GpuGeometryVisibility::BufferPool', True),
    ):
        result += emit_block(source, label + '/GpuGeometryVisibility.cpp', anchor, semicolon)
    if 'class MeshletGroupLookup final' in source:
        lookup, line = block(source, 'class MeshletGroupLookup final', True)
        begin = lookup.index('{') + 1
        lookup = lookup[:begin] + '\n    template<class> friend struct ::PreparationAccess;' + lookup[begin:]
        result += f'#line {line} "{label}/GpuGeometryVisibility.cpp"\n{lookup}\n'
    for anchor in (
        'GpuGeometryVisibility::~GpuGeometryVisibility()',
        'GpuGeometryVisibility::Frame::~Frame()',
        'bool GpuGeometryVisibility::MeshletQueueStorageWords(',
        'bool GpuGeometryVisibility::PrepareMeshlets(',
        'bool GpuGeometryVisibility::PrepareInternal(',
        'bool GpuGeometryVisibility::Prepare(',
        'void GpuGeometryVisibility::Frame::ConsumeStreamingFeedback(',
        'void GpuGeometryVisibility::ShutdownAfterIdle()',
        'void GpuGeometryVisibility::OnUploadSubmitted(',
        'void GpuGeometryVisibility::OnUploadCompleted(',
        'void GpuGeometryVisibility::OnUploadSubmissionRejected(',
        'void GpuGeometryVisibility::OnUploadAborted(',
    ):
        result += emit_block(source, label + '/GpuGeometryVisibility.cpp', anchor)
    # Shader compilation and native pipeline creation are the replaced RHI boundary.
    # Both variants receive the same retained, prewarmed pipeline generations.
    for name in ('Initialize', 'InitializeMeshlets', 'InitializeSharedMeshlets',
                 'InitializeMeshletOcclusion', 'InitializeOcclusion'):
        result += (f'bool GpuGeometryVisibility::{name}(const EnhancedFrameContext& context, std::string& error) '
                   f'{{ return context.resources->InitializePipeline("{name}", error); }}\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', choices=('none', 'address', 'thread'), default='none')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--mutation', choices=('none', 'hash-only', 'borrow-active'), default='none',
                        help='Apply a deliberate defect only to the temporary extracted optimized body; require detection.')
    parser.add_argument('--baseline', default='001ff417b7745401622f4f9ab8e8b9b15fb9ab4d')
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    if args.benchmark and (args.sanitize != 'none' or args.mutation != 'none'):
        parser.error('Benchmark requires an unsanitized, unmutated executable.')
    root = args.source_root.resolve()
    paths = ['Engine/RenderEngine/GpuGeometryVisibility.cpp',
             'Engine/RenderEngine/GpuGeometryVisibility.h']
    source_bytes = [(root / path).read_bytes() for path in paths]
    changed = [contents.decode('utf-8-sig') for contents in source_bytes]
    hashes = {path: hashlib.sha256(contents).hexdigest() for path, contents in zip(paths, source_bytes)}
    baseline = [subprocess.check_output(['git', '-C', str(root), 'show', f'{args.baseline}:{path}'],
                                       text=True, encoding='utf-8-sig') for path in paths]
    if 'class MeshletGroupLookup final' not in changed[0]:
        parser.error('Expected optimized meshlet preparation source.')
    probe = Path(__file__).with_name('meshlet_preparation_cpu_probe.cpp').resolve()
    probe_bytes = probe.read_bytes()
    probe_hash = hashlib.sha256(probe_bytes).hexdigest()
    with tempfile.TemporaryDirectory(prefix='creator-meshlet-preparation-') as directory:
        output = Path(directory)
        captured_probe = output / probe.name
        captured_probe.write_bytes(probe_bytes)
        for label, contents in (('Baseline', baseline), ('Optimized', changed)):
            extracted = variant(*contents, label)
            if label == 'Optimized' and args.mutation != 'none':
                before, after = {
                    'hash-only': ('slot.m_hash == hash && matches(slot.m_bin)', 'slot.m_hash == hash'),
                    'borrow-active': ('m_owned = !scratch.m_active;', 'm_owned = true;'),
                }[args.mutation]
                if extracted.count(before) != 1:
                    raise RuntimeError('Expected one exact production mutation site.')
                extracted = extracted.replace(before, after, 1)
            (output / (label + '.inl')).write_text(extracted, encoding='utf-8')
        executable = output / 'meshlet-preparation-cpu'
        command = [args.compiler, '-std=c++23', '-O2', '-g', '-pthread', '-DCE_CONTRACT_STANDALONE=1',
                   '-DCE_SHIPPING=1', '-Wall', '-Wextra', '-Werror', '-I' + str(output),
                   '-I' + str(probe.parent),
                   '-I' + str(root / 'Engine/Utility_Framework'), '-I' + str(root / 'Engine/RenderEngine'),
                   str(captured_probe), '-o', str(executable)]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                        '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True, timeout=120)
        if args.benchmark:
            compiler = subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0]
            quota = Path('/sys/fs/cgroup/cpu.max')
            print(f'BENCHMARK_ENV compiler={compiler} optimization=O2 '
                  f'affinity={sorted(os.sched_getaffinity(0))} '
                  f'cpu.max={quota.read_text().strip() if quota.is_file() else "unavailable"}', flush=True)
        result = subprocess.run([str(executable), *(['--benchmark'] if args.benchmark else [])],
                                capture_output=True, text=True, timeout=120)
        if args.mutation != 'none':
            if result.returncode != 1 or 'MESHLET_PREPARATION_CPU_FAIL ' not in result.stderr:
                raise RuntimeError(f'Mutation was not caught by a controlled assertion: {result.stdout}{result.stderr}')
            print(f'MUTATION_CAUGHT {args.mutation} {result.stderr.strip()}', flush=True)
        else:
            print(result.stdout, end='', flush=True)
            if result.returncode:
                raise RuntimeError(result.stderr or f'Probe exited with status {result.returncode}.')
    for path, digest in hashes.items():
        if hashlib.sha256((root / path).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Production source changed during verification: {path}')
        print(f'PRODUCTION_SHA256 {path} {digest}')
    if hashlib.sha256(probe.read_bytes()).hexdigest() != probe_hash:
        raise RuntimeError('Probe source changed during verification.')
    print(f'BASE_COMMIT {args.baseline}')
    print(f'PROBE_SHA256 {probe.name} {probe_hash}')
    print('SCOPE exact-production-prepare metadata-and-owner-parity fake-RHI no-GPU no-frame-speedup')


if __name__ == '__main__':
    main()
