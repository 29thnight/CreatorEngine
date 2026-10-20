#!/usr/bin/env python3
"""Execute actual nonshipping profiler RHI sidecar/codec/ownership contracts.

All EngineDiagnostics translation units and transitive project headers compile
unchanged from byte snapshots. The Windows QPC/thread-id/process-counter boundary
uses a small POSIX-host shim; real collector, producer, capture ring, snapshot and
streaming codecs execute. This is not a Windows product/ImGui/runtime acceptance
or a native GPU timing test. --compat also runs the existing profiler core and
render-measurement regression programs against the same compiled objects.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
CORE = Path('Engine/EngineDiagnostics')
SOURCES = [CORE / (name + '.cpp') for name in (
    'ProfileMarker', 'ProfileThreadStream', 'ProfileCapture', 'ProfileCaptureFile',
    'ProfileRecording', 'ProfileAggregate', 'ProfileReader', 'ProfileService')]
PROBE = Path('Tools/regression/profile_rhi_context_probe.cpp')
COMPAT = [Path('Tools/regression/profile_core_probe.cpp'),
          Path('Tools/regression/ProfilerRenderMeasurementTests.cpp')]

WINDOWS = r'''#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
using DWORD=std::uint32_t; using BOOL=int; using HANDLE=void*;
struct LARGE_INTEGER { long long QuadPart; };
struct FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; };
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* p) { p->QuadPart=1000000000; return 1; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* p) {
    p->QuadPart=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(); return 1;
}
inline DWORD GetCurrentThreadId() {
    static std::atomic<DWORD> next{1}; thread_local const DWORD id=next.fetch_add(1); return id;
}
inline HANDLE GetCurrentProcess() { return nullptr; }
inline void GetSystemTimeAsFileTime(FILETIME* p) {
    const auto value=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()/100);
    p->dwLowDateTime=static_cast<DWORD>(value); p->dwHighDateTime=static_cast<DWORD>(value>>32);
}
inline BOOL GetProcessTimes(HANDLE, FILETIME*, FILETIME*, FILETIME*, FILETIME*) { return 0; }
inline DWORD GetActiveProcessorCount(unsigned short) { return std::thread::hardware_concurrency(); }
inline constexpr unsigned short ALL_PROCESSOR_GROUPS=0xffff;
'''
PSAPI = '''#pragma once
#include "Windows.h"
#include <cstddef>
struct PROCESS_MEMORY_COUNTERS { std::size_t WorkingSetSize{}; };
inline BOOL K32GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD) { return 0; }
'''
CRT = r'''#pragma once
#include "Windows.h"
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
inline int getenv_s(std::size_t* length, char* buffer, std::size_t capacity, const char* name) {
    if (!length || !name || (!buffer && capacity)) return EINVAL;
    const char* value=std::getenv(name); *length=value ? std::strlen(value)+1 : 0;
    if (!buffer) return 0;
    if (capacity==0) return ERANGE;
    buffer[0]=0;
    if (!value) return 0;
    if (*length>capacity) return ERANGE;
    std::memcpy(buffer,value,*length); return 0;
}
'''
ALLOCATION_BOUNDARY = r'''#include <new>
#include <cstddef>
// Both probes replace throwing new/delete with an allocation watch backed by
// malloc/free. libstdc++ stable_sort uses nothrow new, so route that overload to
// the same replacement instead of ASan's unmatched default operator new.
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return ::operator new(bytes); } catch (...) { return nullptr; }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { ::operator delete(value); }
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--compat', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keep-build', type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error('C++23 compiler required')
    probes = [PROBE] + (COMPAT if args.compat else [])
    snapshots = {}
    pending = [root / p for p in SOURCES + probes]
    while pending:
        path = pending.pop().resolve()
        if path in snapshots:
            continue
        snapshots[path] = path.read_bytes()
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', snapshots[path].decode('utf-8-sig'), re.M):
            found = next((candidate.resolve() for candidate in
                          (path.parent / include, root / include, root / CORE / include)
                          if candidate.is_file()), None)
            if found is None:
                raise RuntimeError(f'unresolved project include {include} in {path}')
            pending.append(found)
    runner = Path(__file__).resolve()
    snapshots[runner] = runner.read_bytes()
    if args.compat:
        for name in ('profile-v1.ceprof', 'profile-counter-v1.ceprof'):
            fixture = root / 'Tools/regression/fixtures/physics-p3' / name
            snapshots[fixture] = fixture.read_bytes()
    contracts = root / 'Tools/build/Contracts.targets'
    snapshots[contracts] = contracts.read_bytes()
    config = ET.fromstring(snapshots[contracts]).find(
        './Target[@Name="GenerateCreatorContractConfig"]/WriteLinesToFile').attrib['Lines']
    config = config.replace('%0A', '\n').replace('%3B', ';').replace('$(_CreatorAssertionsEnabled)', 'true') + '\n'
    with tempfile.TemporaryDirectory(prefix='profile-rhi-') as temporary:
        target = args.keep_build.resolve() if args.keep_build else Path(temporary)
        target.mkdir(parents=True, exist_ok=True)
        mirror = target / 'mirror'
        for path, data in snapshots.items():
            destination = mirror / path.relative_to(root)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        include = target / 'include'
        include.mkdir(exist_ok=True)
        (include / 'Windows.h').write_text(WINDOWS)
        (include / 'psapi.h').write_text(PSAPI)
        (include / 'CreatorContractConfig.h').write_text(config)
        (include / 'crt-boundary.h').write_text(CRT)
        allocation_boundary = target / 'probe-allocation-boundary.cpp'
        allocation_boundary.write_text(ALLOCATION_BOUNDARY)
        flags = [compiler, '-std=c++23', '-O1', '-g', '-pthread', '-DCE_CONTRACT_STANDALONE=1',
                 '-DCE_SHIPPING=0', '-Wall', '-Wextra', '-Wno-unused-parameter',
                 '-I', str(include), '-I', str(mirror), '-I', str(mirror / CORE),
                 '-include', str(include / 'crt-boundary.h')]
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        commands, outputs, objects = [], [], []
        print('Compiling unchanged nonshipping profiler implementation with QPC/platform-counter boundary shim', flush=True)
        for source in SOURCES:
            obj = target / (source.stem + '.o')
            command = flags + ['-c', str(mirror / source), '-o', str(obj)]
            commands.append(command)
            built = subprocess.run(command, text=True, capture_output=True, timeout=180)
            outputs.append(built.stdout + built.stderr)
            (target / 'compile.log').write_text(''.join(outputs))
            if built.returncode:
                print(outputs[-1])
                built.check_returncode()
            objects.append(str(obj))
        environment = os.environ.copy()
        if args.sanitize:
            environment.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        runs = []
        for probe in probes:
            executable = target / probe.stem
            command = flags + [str(mirror / probe), *objects, '-o', str(executable)]
            if probe == PROBE or probe.name == 'profile_core_probe.cpp':
                command.append(str(allocation_boundary))
            commands.append(command)
            built = subprocess.run(command, text=True, capture_output=True, timeout=180)
            outputs.append(built.stdout + built.stderr)
            (target / 'compile.log').write_text(''.join(outputs))
            if built.returncode:
                print(outputs[-1])
                built.check_returncode()
            run_command = [str(executable)]
            if probe == PROBE:
                run_command.append(tempfile.mkdtemp(prefix='captures-', dir=target))
            result = subprocess.run(run_command, text=True, capture_output=True, env=environment, cwd=mirror, timeout=180)
            (target / (probe.stem + '.log')).write_text(result.stdout + result.stderr)
            print(result.stdout, end='')
            print(result.stderr, end='')
            result.check_returncode()
            runs.append({'probe': str(probe), 'command': run_command, 'exit': result.returncode,
                         'binary_sha256': digest(executable.read_bytes()), 'stdout': result.stdout,
                         'stderr': result.stderr})
        for path, data in snapshots.items():
            if path.read_bytes() != data:
                raise RuntimeError(f'source changed during verification: {path}')
        evidence = {'recorded_utc': datetime.now(timezone.utc).isoformat(), 'boundary': __doc__,
                    'sanitized': args.sanitize, 'compatibility_probes_run': args.compat,
                    'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                    'compiler_commands': commands, 'compile_exit': 0, 'runs': runs,
                    'source_sha256': {str(p.relative_to(root)): digest(b) for p,b in snapshots.items()},
                    'platform_shim_sha256': {'Windows.h': digest(WINDOWS.encode()), 'psapi.h': digest(PSAPI.encode()),
                                            'crt-boundary.h': digest(CRT.encode())},
                    'probe_allocation_boundary': ALLOCATION_BOUNDARY,
                    'probe_allocation_boundary_sha256': digest(ALLOCATION_BOUNDARY.encode()),
                    'generated_contract_config_sha256': digest(config.encode()),
                    'contract_configuration': 'Debug assertions=true, generated verbatim from Contracts.targets',
                    'source_unchanged_after_execution': True, 'native_windows_executed': False,
                    'native_gpu_executed': False,
                    'sanitizer_environment': {k: environment[k] for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS')}
                        if args.sanitize else {},
                    'memory_budget_contract': 'Page pool cap is max_chunk_count * sizeof(event_chunk); capture ring and frame_record account actual sizeof(profile_render_measurement). Thread producer metadata uses two fixed arrays of kMaxScopeDepth; OFF scopes add no heap allocation. No claim that memory overhead is zero.',
                    'sanitizer_note': 'Fail-fast ASan/UBSan. LeakSanitizer is disabled because of restricted /proc access; no whole-process leak claim.'}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(evidence, indent=2) + '\n')


if __name__ == '__main__':
    main()
