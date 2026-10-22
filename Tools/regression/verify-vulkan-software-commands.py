#!/usr/bin/env python3
"""Compile and exercise Vulkan software recording against the official SDK ABI.

The Vulkan command pool, recording snapshot, validator, encoder, upload/descriptor
allocators, resource/pipeline registries, common recorder/translator/FIFO and
JobScheduler/enkiTS are the actual source. Vulkan function pointers are CPU mocks;
this does not run a Vulkan device or validate rendered pixels. Shared-image
construction and rollback use the actual VulkanSharedDisplayImage implementation;
D3D12/DXGI/WRL declarations and calls are CPU mocks, not a Windows SDK or COM ABI
validation. DX12 exact subresource lowering uses the same declared native boundary.
A mirrored header
excludes unrelated texture-cache declarations/COM and trailing mesh declarations.
One pre-existing GCC aggregate assignment is spelled with its explicit type in
that mirror. Windows handle/environment/debug calls and the Win32 profiler's
thread-name registration are mock boundaries. Device Initialize/Shutdown and
admission/submit bodies are exact; physical-device creation, WSI, prefix flush,
memory-budget refresh and lifecycle-idle proof are mocks. Completion advances
only when the fixture explicitly advances its fake fence. The mimalloc boundary
uses libc allocation; the warm snapshot counter observes standard operator new.
Production is never edited, and neither renderer execution nor pixels are tested.
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
RHI = Path('Engine/RenderEngine/RHI')
VK = RHI / 'Vulkan'
CPP = [VK / (name + '.cpp') for name in (
    'VulkanRecordingResources', 'VulkanSoftwareCommandValidator', 'VulkanEncoder',
    'VulkanCommandBufferPool', 'VulkanFrameAllocators', 'VulkanResourceTable', 'VulkanPipelineCache',
    'VulkanSharedDisplayImage')]
CPP += [RHI / (name + '.cpp') for name in (
    'RHICommandTranslator', 'RHISoftwareCommandList', 'RHISoftwareEncoder',
    'RHISoftwareBatch', 'RHISoftwareRecorder', 'RHISubmissionThread', 'RHIDeviceMemoryBudgetCoordinator')]
CPP += [Path('Engine/RenderEngine/RenderArena.cpp'), Path('Engine/Utility_Framework/JobScheduler.cpp'),
        Path('Engine/Utility_Framework/ThreadPool.cpp'), Path('Engine/Utility_Framework/Arena.cpp')]
DEVICE_METHODS = ('Initialize', 'Shutdown', 'PrepareParallelSubmission', 'AcceptParallelSubmission',
                  'RejectNeverEnqueuedRecording', 'SubmitParallelCommandBuffers', 'FindMemoryType')
SERVICE_METHODS = ('ReleaseTexture', 'DescribeTexture')


def balanced_end(text, begin):
    level = 0
    # Called at a class/function opening brace; these bodies contain no brace
    # characters inside string literals. Verify extraction still ends on a brace.
    for index in range(begin, len(text)):
        level += (text[index] == '{') - (text[index] == '}')
        if level == 0:
            return index + 1
    raise RuntimeError('unclosed extracted source body')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--vulkan-headers', type=Path, required=True)
    parser.add_argument('--enkits', type=Path, required=True)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keep-build', type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    sdk = args.vulkan_headers.resolve()
    if (sdk / 'include/vulkan/vulkan_core.h').is_file():
        sdk /= 'include'
    if not (sdk / 'vulkan/vulkan_core.h').is_file():
        parser.error('--vulkan-headers must contain official Vulkan headers')
    enkits = args.enkits.resolve()
    if (enkits / 'src/TaskScheduler.cpp').is_file():
        enkits /= 'src'
    compiler = shutil.which(args.compiler)
    if not compiler or not (enkits / 'TaskScheduler.cpp').is_file():
        parser.error('C++23 compiler and enkiTS source are required')
    snapshots = {}
    adapted = {}
    pending = [root / path for path in CPP]
    probe = Path(__file__).with_name('vulkan_software_commands_probe.cpp').resolve()
    pending += [probe, root / VK / 'VulkanCaptureGpuProfiler.h']
    adaptations = {}
    device_header = ''
    while pending:
        path = pending.pop().resolve()
        if path in snapshots:
            continue
        data = path.read_bytes()
        snapshots[path] = data
        text = data.decode('utf-8-sig')
        if path == root / RHI / 'IRenderDeviceServices.h':
            text = text.split('/// Borrowed mesh metadata', 1)[0]
            text = re.sub(r'^#include "../Assets/Model(?:AssetGeneration|GeometryPayload).h"\n', '', text, flags=re.M)
            adaptations[str(path.relative_to(root))] = 'Exclude trailing mesh declarations and their two Assets includes; complete service interface unchanged.'
        if path == root / VK / 'VulkanDeviceResources.h':
            start = text.index('class VulkanDeviceResources :')
            end = balanced_end(text, text.index('{', start)) + 1
            text = text[:end] + '\n'
            text = text.replace('#include "../IRenderTextureCache.h"\n', '')
            device_header = text[text.index('class VulkanDeviceResources :'):]
            adaptations[str(path.relative_to(root))] = 'Exclude unrelated trailing VulkanTextureCache and its COM-dependent header; complete device class unchanged.'
        if path == root / RHI / 'RHIGraphicsPipelineRequest.h':
            assert text.count('other.m_desc = {};') == 1
            text = text.replace('other.m_desc = {};', 'other.m_desc = RHIGraphicsPipelineDesc{};')
            adaptations[str(path.relative_to(root))] = 'GCC compatibility spelling of pre-existing aggregate assignment; no Vulkan changed body adapted.'
        if path == root / 'Engine/EngineDiagnostics/ProfileService.h':
            text = '''#pragma once
#include "ProfileThreadStream.h"
namespace ce {
class profiler_service {
public:
    void register_thread(const char*, track_kind, uint32_t) {}
    void unregister_thread() {}
};
inline profiler_service& profiler() { static profiler_service value; return value; }
}
'''
            adaptations[str(path.relative_to(root))] = 'No-op Win32 profiler thread-name registration boundary only; CE_SHIPPING removes profile events.'
        adapted[path] = text
        includes = re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M)
        includes += re.findall(r'^\s*#\s*include\s*<(mathematics/[^>]+)>', text, re.M)
        for include in includes:
            if include == 'vulkan_device_boundary.inc':
                continue
            candidates = (path.parent / include, root / include,
                          root / 'Engine/Utility_Framework' / include,
                          root / 'ThirdParty/Mathematics/include' / include)
            found = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if found is None:
                raise RuntimeError(f'unresolved include {include} in {path}')
            if include.endswith('mimalloc.h'):
                snapshots[found] = found.read_bytes()
            else:
                pending.append(found)
    source = root / VK / 'VulkanDeviceResources.cpp'
    snapshots[source] = source.read_bytes()
    device_source = snapshots[source].decode('utf-8-sig')
    methods = []
    body_hashes = {}
    services_source = root / VK / 'VulkanRenderServices.cpp'
    snapshots[services_source] = services_source.read_bytes()
    for source_text, names in ((device_source, DEVICE_METHODS),
                              (snapshots[services_source].decode('utf-8-sig'), SERVICE_METHODS)):
        for method in names:
            match = re.search(r'\w+ VulkanDeviceResources::' + method + r'\(', source_text)
            assert match, f'could not extract native method {method}'
            end = balanced_end(source_text, source_text.index('{', match.start()))
            body = source_text[match.start():end]
            if source_text[end:end + 6] == ' const':
                raise RuntimeError('method extraction missed a const qualifier')
            methods.append(body)
            body_hashes[method] = hashlib.sha256(body.encode()).hexdigest()
    # Uncalled device/WSI endpoints fail explicitly at the mock boundary.
    clean = re.sub(r'//[^\n]*|/\*.*?\*/', '', device_header, flags=re.S)
    stubs = []
    for match in re.finditer(r'([\w:<>*&\s]+?)\s+([\w~]+)\s*\(([^;{}]*)\)\s*(const\s*)?override\s*;', clean):
        result, name, parameters, qualifier = match.groups()
        result = result.strip().split('\n')[-1].strip()
        if name in DEVICE_METHODS + SERVICE_METHODS or name in ('~VulkanDeviceResources', 'GetCompletedFenceValue', 'FlushCommandList'):
            continue
        parameters = re.sub(r'\s*=\s*[^,)]*', '', parameters)
        stubs.append(f'{result} VulkanDeviceResources::{name}({parameters}) {qualifier or ""}'
                     '{ throw std::logic_error("Unexpected device/WSI mock endpoint: ' + name + '"); }')
    runner = Path(__file__).resolve()
    snapshots[runner] = runner.read_bytes()
    for path in sdk.rglob('*.h'):
        snapshots[path] = path.read_bytes()
    for path in enkits.glob('*'):
        if path.suffix in ('.h', '.cpp'):
            snapshots[path] = path.read_bytes()
    with tempfile.TemporaryDirectory(prefix='vulkan-software-') as temporary:
        target = args.keep_build.resolve() if args.keep_build else Path(temporary)
        target.mkdir(parents=True, exist_ok=True)
        mirror = target / 'mirror'
        for path, text in adapted.items():
            destination = mirror / path.relative_to(root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(text)
        boundary = mirror / 'Tools/regression/vulkan_device_boundary.inc'
        boundary.write_text('\n'.join(stubs + methods))
        include = target / 'include'
        include.mkdir(exist_ok=True)
        (include / 'windows.h').write_text('''#pragma once
#include <cstdint>
#define WINAPI
#define APIENTRY
#define CALLBACK
using HANDLE=void*; using HINSTANCE=void*; using HWND=void*; using HMONITOR=void*;
using LPCWSTR=const wchar_t*; using DWORD=uint32_t;
struct LUID { uint32_t LowPart; int32_t HighPart; };
struct SECURITY_ATTRIBUTES;
inline DWORD GetEnvironmentVariableA(const char*, char*, DWORD) { return 0; }
inline void OutputDebugStringA(const char*) {}
''')
        interop = root / 'Tools/regression/fixtures/rhi-d3d12-interop-boundary.h'
        (include / interop.name).write_bytes(snapshots[interop])
        for native_header in ('d3d12.h', 'dxgi1_6.h', 'wrl/client.h'):
            destination = include / native_header
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text('#include "rhi-d3d12-interop-boundary.h"\n')
        for path, data in list(snapshots.items()):
            if path.is_relative_to(sdk):
                out = include / path.relative_to(sdk)
            elif path.is_relative_to(enkits):
                out = include / 'enkiTS' / path.relative_to(enkits)
            else:
                continue
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(data)
        allocator = mirror / 'ThirdParty/mimalloc/include/mimalloc.h'
        allocator.parent.mkdir(parents=True, exist_ok=True)
        allocator.write_text('''#pragma once
#include <cstddef>
#include <cstdlib>
inline void* mi_malloc(std::size_t n) { return std::malloc(n); }
inline void* mi_malloc_aligned(std::size_t n,std::size_t a)
{ void* p{}; if(posix_memalign(&p,a<sizeof(void*)?sizeof(void*):a,n)) return nullptr; return p; }
inline void mi_free(void* p) { std::free(p); }
''')
        executable = target / 'vulkan-software'
        command = [compiler, '-std=c++23', '-O1', '-g', '-pthread', '-DCE_CONTRACT_STANDALONE=1',
                   '-DCE_SHIPPING=1', '-DCE_DX_TIMING_CAPTURE=0', '-Wall', '-Wextra',
                   '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Wno-unknown-pragmas',
                   '-I', str(include), '-I', str(mirror), '-I', str(mirror / 'Engine/Utility_Framework'),
                   '-I', str(mirror / 'ThirdParty/Mathematics/include'),
                   str(mirror / probe.relative_to(root))]
        command += [str(mirror / path) for path in CPP]
        command += [str(include / 'enkiTS/TaskScheduler.cpp'), '-o', str(executable)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        print('Compiling exact Vulkan implementations with official Vulkan SDK types and CPU native API mocks', flush=True)
        built = subprocess.run(command, text=True, capture_output=True, timeout=300)
        (target / 'compile.log').write_text(built.stdout + built.stderr)
        if built.returncode:
            print(built.stdout + built.stderr)
            built.check_returncode()
        environment = os.environ.copy()
        if args.sanitize:
            environment.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = subprocess.run([str(executable)], text=True, capture_output=True, env=environment, timeout=90)
        print(result.stdout, end='')
        if result.stderr:
            print(result.stderr, end='')
        result.check_returncode()
        for path, data in snapshots.items():
            if path.read_bytes() != data:
                raise RuntimeError(f'source changed during verification: {path}')
        evidence = {'sanitized': args.sanitize, 'boundary': __doc__, 'adaptations': adaptations,
                    'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                    'command': command, 'compile_exit': built.returncode, 'run_exit': result.returncode,
                    'binary_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
                    'source_sha256': {str(p.relative_to(root) if p.is_relative_to(root) else p): hashlib.sha256(b).hexdigest()
                                      for p,b in snapshots.items()}, 'device_body_sha256': body_hashes,
                    'result': [json.loads(line) for line in result.stdout.splitlines() if line.strip()],
                    'source_unchanged_after_execution': True, 'native_gpu_executed': False}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(evidence, indent=2) + '\n')


if __name__ == '__main__':
    main()
