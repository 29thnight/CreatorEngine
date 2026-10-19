#!/usr/bin/env python3
"""실제 primitive cache의 C++ 수명/그래프/PSO 배선을 portable CPU 경계에서 검증한다.

장치 서비스만 fixture로 대체한다. Windows RHI 실행이나 GPU 성능 검사는 아니다.
컴파일러/의존성을 다운로드하지 않으며 읽은 원문 bytes의 hash를 결과에 기록한다.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HOST = Path('Engine/RenderEngine/MaterialGraphSceneHost.cpp')
CACHE = Path('Engine/RenderEngine/MaterialGraphPrimitiveCache.h')
GPU_DATA = Path('Engine/RenderEngine/MaterialGraphSceneGpuData.h')
GRAPH = Path('Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h')
SHADERS = Path('Dynamic_CPP/Assets/Shaders/DefaultPassShader')


def block(source, anchor):
    start = source.index(anchor)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: ''.join('\n' if char == '\n' else ' ' for char in match.group()), source)
    begin = masked.index('{', start)
    if anchor.startswith(('bool ', 'void ')):
        # Default RHIBufferHandle{} arguments occur before the function body.
        arguments = masked.index('(', start)
        parentheses = 0
        for index in range(arguments, len(masked)):
            parentheses += (masked[index] == '(') - (masked[index] == ')')
            if parentheses == 0:
                begin = masked.index('{', index)
                break
    depth = 0
    for index in range(begin, len(masked)):
        depth += (masked[index] == '{') - (masked[index] == '}')
        if depth == 0:
            return source[start:index + 1], source.count('\n', 0, start) + 1
    raise ValueError(f'Unclosed production block: {anchor}')


def emit(source, path, anchor):
    text, line = block(source, anchor)
    return f'#line {line} "{path}"\n{text}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--slangc', type=Path, help='설치된 Slang으로 실제 cache 변환/조회 CPU oracle도 실행한다.')
    parser.add_argument('--output', type=Path, help='검증 json을 저장한다.')
    parser.add_argument('--output-executable', type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    probe = Path(__file__).with_name('primitive_cache_lifetime_probe.cpp')
    paths = [root / path for path in (HOST, CACHE, GPU_DATA, GRAPH,
             Path('Engine/RenderEngine/MaterialGraphPrimitiveCacheContract.h'),
             Path('Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h'),
             Path('Engine/RenderEngine/RHI/RHIResourceTypes.h'),
             Path('Engine/RenderEngine/RHI/RHIPipelineState.h'),
             Path('Engine/RenderEngine/RHI/RHIPipelineLayout.h'),
             Path('Engine/RenderEngine/RHI/RHIShaderBlob.h'),
             Path('ThirdParty/ownership_cpp/include/own/ownership.hpp'))] + [probe, Path(__file__)]
    snapshots = {path: path.read_bytes() for path in paths}
    shader_probe = Path(__file__).with_name('reac_primitive_cache_probe.slang')
    shader_host = shader_probe.with_suffix('.cpp')
    if args.slangc:
        pending = [shader_probe, shader_host]
        while pending:
            path = pending.pop()
            if path in snapshots:
                continue
            data = path.read_bytes()
            snapshots[path] = data
            if path.suffix != '.slang':
                continue
            for include in re.findall(r'^\s*#include\s*"([^"]+)"', data.decode('utf-8-sig'), re.MULTILINE):
                candidates = [path.parent / include, root / SHADERS / include]
                target = next((item for item in candidates if item.is_file()), None)
                if target is None:
                    raise RuntimeError(f'Unresolved shader include: {path}: {include}')
                pending.append(target)
    hashes = {str(path): hashlib.sha256(data).hexdigest() for path, data in snapshots.items()}
    source = snapshots[root / HOST].decode('utf-8-sig')
    graph = snapshots[root / GRAPH].decode('utf-8-sig')
    environment = dict(os.environ)
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    with tempfile.TemporaryDirectory(prefix='creator-primitive-cache-') as folder:
        output = Path(folder)
        (output / probe.name).write_bytes(snapshots[probe])
        declarations = re.sub(r'^#include[^\n]*', '', snapshots[root / CACHE].decode('utf-8-sig'), flags=re.MULTILINE)
        (output / 'PrimitiveCacheProduction.inl').write_text(f'#line 1 "{root / CACHE}"\n' + declarations, encoding='utf-8')
        (output / 'PrimitiveCachePacketBindings.inl').write_text(
            emit(snapshots[root / GPU_DATA].decode('utf-8-sig'), root / GPU_DATA, 'bool CreatePacketBindings('),
            encoding='utf-8')
        (output / 'PrimitiveCacheBindReconstruction.inl').write_text(
            emit(source, root / HOST, 'void BindReconstruction('), encoding='utf-8')
        (output / 'PrimitiveCacheAdmission.inl').write_text(
            emit(source, root / HOST, 'if (const auto& work = candidate->materialWorkStage.materialWork; work)'),
            encoding='utf-8')
        first = graph.index('enum class RGResourceKind')
        last = graph.index('// Access/version declarations', first)
        (output / 'PrimitiveCacheGraphTypes.inl').write_text(graph[first:last], encoding='utf-8')
        executable = output / 'primitive_cache_lifetime_probe'
        command = [args.compiler, '-std=c++23', '-O1' if args.sanitize else '-O2', '-pthread',
                   '-DCE_CONTRACT_STANDALONE', '-DCE_SHIPPING=1', '-ffp-contract=off',
                   '-I', str(root / 'Engine/RenderEngine'), '-I', str(root / 'Engine/Utility_Framework'),
                   '-I', str(output), str(output / probe.name), '-o', str(executable)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        print('COMPILE exact cache C++ header, packet bindings, Host admission and PSO selection', flush=True)
        subprocess.run(command, check=True, timeout=120, env=environment)
        result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=60, env=environment)
        print(result.stdout, end='')
        print(result.stderr, end='')
        if args.output_executable and result.returncode == 0:
            args.output_executable.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(executable, args.output_executable)
        report = {'scope': 'exact-production-cpp-with-device-graph-boundary-fixtures',
                  'native_gpu': False, 'native_windows_rhi': False, 'sanitize': args.sanitize,
                  'command': command, 'exit_code': result.returncode,
                  'stdout': result.stdout, 'stderr': result.stderr, 'source_sha256': hashes}
        if args.slangc and result.returncode == 0:
            frozen = output / 'sources'
            for path, data in snapshots.items():
                if path.suffix == '.slang' or path == shader_host:
                    destination = frozen / path.relative_to(root)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(data)
            generated = output / 'primitive-cache.generated.cpp'
            shader_command = [str(args.slangc.resolve()), str(frozen / shader_probe.relative_to(root)),
                              '-I', str(frozen / SHADERS), '-entry', 'PrimitiveCacheOracleCS',
                              '-stage', 'compute', '-target', 'cpp', '-matrix-layout-column-major',
                              '-O3', '-fp-mode', 'precise', '-warnings-as-errors', 'all', '-o', str(generated)]
            print('COMPILE and RUN exact Slang cache transform/lookup CPU oracle', flush=True)
            subprocess.run(shader_command, check=True, timeout=120, env=environment)
            shader_binary = output / 'primitive_cache_shader_probe'
            host_command = [args.compiler, '-std=c++20', '-O1' if args.sanitize else '-O2', '-fno-fast-math',
                            '-ffp-contract=off', '-I', str(output), str(frozen / shader_host.relative_to(root)),
                            '-o', str(shader_binary)]
            if args.sanitize:
                host_command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
            subprocess.run(host_command, check=True, timeout=120, env=environment)
            shader_result = subprocess.run([str(shader_binary)], text=True, capture_output=True,
                                           timeout=60, env=environment)
            print(shader_result.stdout, end='')
            print(shader_result.stderr, end='')
            report['shader_cpu'] = {'slang_command': shader_command, 'host_command': host_command,
                                    'generated_sha256': hashlib.sha256(generated.read_bytes()).hexdigest(),
                                    'exit_code': shader_result.returncode, 'stdout': shader_result.stdout,
                                    'stderr': shader_result.stderr,
                                    'precise_cpp_token': 'compatibility macro; fno-fast-math and ffp-contract=off'}
            if shader_result.returncode:
                result = shader_result
                report['exit_code'] = result.returncode
    changed = [path for path in snapshots if path.read_bytes() != snapshots[path]]
    if changed:
        raise RuntimeError(f'Production or probe changed during verification: {changed}')
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    if result.returncode:
        raise SystemExit(result.returncode)
    print('PRIMITIVE_CACHE_SOURCE_SHA256 ' + json.dumps(hashes, sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
