"""Generate source-tool headers from the authoritative named module interfaces.

Native targets import the modules. Clang reflection parsing and standalone probes
explicitly select CE_CONTRACT_STANDALONE and use these generated headers instead.
Run with --check in validation, or without arguments after changing an interface.
"""
from pathlib import Path
import argparse
import re
import os

ROOT = Path(__file__).resolve().parents[2]
MODULES = (
    "Creator.Core.Delegate",
    "Creator.Concurrency.Jobs",
    "Creator.Core.Uuid",
    "Creator.Core.Layers",
    "Creator.Core.BitFlag",
    "Creator.Core.DynamicBitset",
    "Creator.Core.WorkPartition",
    "Creator.Core.SpinLock",
    "Creator.Core.Stopwatch",
    "Creator.Core.RingBuffer",
    "Creator.Core.BoundedMemory",
    "Creator.Core.Base64",
    "Creator.Core.ContentAbi",
    "Creator.Core.LayerCatalog",
    "Creator.Core.EngineMode",
    "Creator.Core.Progress",
    "Creator.Core.Ownership",
    "Creator.Core.HeapAllocator",
    "Creator.Core.Arena",
    "Creator.Core.LifetimeToken",
    "Creator.Core.Paths",
    "Creator.Core.Distribution",
    "Creator.Concurrency.JobGraph",
    "Creator.Core.Sha256",
    "Creator.Core.Strings",
    "Creator.Platform.Windows.Strings",
    "Creator.Contracts",
    "Creator.Concurrency",
    "Creator.Core.Window",
    "Creator.Platform.Windows",
    "Creator.AppBackend.WinAPI",
)


MODULE_SOURCES = {
    "Creator.Core.Delegate": "Engine/Contracts/Core/Creator.Core.Delegate.ixx",
    "Creator.Concurrency.Jobs": "Engine/Contracts/Concurrency/Creator.Concurrency.Jobs.ixx",
    "Creator.Core.Uuid": "Engine/Contracts/Core/Creator.Core.Uuid.ixx",
    "Creator.Core.Layers": "Engine/Contracts/Core/Creator.Core.Layers.ixx",
    "Creator.Core.BitFlag": "Engine/Contracts/Core/Creator.Core.BitFlag.ixx",
    "Creator.Core.DynamicBitset": "Engine/Contracts/Core/Creator.Core.DynamicBitset.ixx",
    "Creator.Core.WorkPartition": "Engine/Contracts/Core/Creator.Core.WorkPartition.ixx",
    "Creator.Core.Stopwatch": "Engine/Contracts/Core/Creator.Core.Stopwatch.ixx",
    "Creator.Core.Base64": "Engine/Contracts/Core/Creator.Core.Base64.ixx",
    "Creator.Core.EngineMode": "Engine/Contracts/Core/Creator.Core.EngineMode.ixx",
    "Creator.Core.Ownership": "Engine/Contracts/Core/Creator.Core.Ownership.ixx",
    "Creator.Core.HeapAllocator": "Engine/Contracts/Core/Creator.Core.HeapAllocator.ixx",
    "Creator.Core.Arena": "Engine/Contracts/Core/Creator.Core.Arena.ixx",
    "Creator.Core.LifetimeToken": "Engine/Contracts/Core/Creator.Core.LifetimeToken.ixx",
    "Creator.Core.Paths": "Engine/Contracts/Core/Creator.Core.Paths.ixx",
    "Creator.Core.Distribution": "Engine/Contracts/Core/Creator.Core.Distribution.ixx",
    "Creator.Core.Progress": "Engine/Contracts/Core/Creator.Core.Progress.ixx",
    "Creator.Core.LayerCatalog": "Engine/Contracts/Core/Creator.Core.LayerCatalog.ixx",
    "Creator.Concurrency.JobGraph": "Engine/Contracts/Concurrency/Creator.Concurrency.JobGraph.ixx",
    "Creator.Core.ContentAbi": "Engine/Contracts/Core/Creator.Core.ContentAbi.ixx",
    "Creator.Core.BoundedMemory": "Engine/Contracts/Core/Creator.Core.BoundedMemory.ixx",
    "Creator.Core.RingBuffer": "Engine/Contracts/Core/Creator.Core.RingBuffer.ixx",
    "Creator.Core.SpinLock": "Engine/Contracts/Core/Creator.Core.SpinLock.ixx",
    "Creator.Core.Sha256": "Engine/Contracts/Core/Creator.Core.Sha256.ixx",
    "Creator.Core.Strings": "Engine/Contracts/Core/Creator.Core.Strings.ixx",
    "Creator.Platform.Windows.Strings": "Engine/Contracts/Platform/Windows/Creator.Platform.Windows.Strings.ixx",
    "Creator.Contracts": "Engine/Contracts/Creator.Contracts.ixx",
    "Creator.Concurrency": "Engine/Contracts/Concurrency/Creator.Concurrency.ixx",
    "Creator.Core.Window": "Engine/Contracts/Core/Creator.Core.Window.ixx",
    "Creator.Platform.Windows": "Engine/Contracts/Platform/Windows/Creator.Platform.Windows.ixx",
    "Creator.AppBackend.WinAPI": "Engine/Platform/Windows/AppBackendWinAPI/Creator.AppBackend.WinAPI.ixx"
}


def render(name):
    source_path = MODULE_SOURCES[name]
    source = (ROOT / source_path).read_text(encoding="utf-8-sig")
    lines = [
        "#pragma once",
        f"// Generated from {source_path}; do not edit.",
        "// Regenerate with: py Tools/build/generate-module-adapters.py",
        "#if !defined(CE_CONTRACT_STANDALONE)",
        '#error "Native consumers must import the named module directly."',
        "#endif",
        "",
    ]
    if name == "Creator.Platform.Windows":
        # Standalone probes do not consume Contracts.targets linker metadata.
        lines += ['#pragma comment(lib, "ole32.lib")', '#pragma comment(lib, "runtimeobject.lib")', ""]
    for line in source.splitlines():
        if line == "module;" or line.startswith("export module "):
            continue
        match = re.fullmatch(r"(?:export )?import ([\w.]+);", line)
        header_unit = re.fullmatch(r'(?:export )?import "(Creator\.[\w.]+\.h)";', line)
        quoted_include = re.fullmatch(r'#include "([^"\n]+)"', line)
        if quoted_include:
            include_path = (ROOT / source_path).parent / quoted_include[1]
            if include_path.is_file():
                relative = Path(os.path.relpath(include_path, ROOT / "Tools/build/ModuleAdapters")).as_posix()
                lines.append(f'#include "{relative}"')
            else:
                lines.append(line)
        elif header_unit:
            provider = ("Engine/Containers/" if header_unit[1] == "Creator.Containers.h" else "Engine/Contracts/External/" if header_unit[1] in ["Creator.Ownership.h", "Creator.MemoryProvider.h"] else "Engine/Platform/WindowsSDK/") + header_unit[1]
            lines.append(f'#include "../../../{provider}"')
        elif match:
            dependency = match[1]
            if dependency == "std":
                lines.append('#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"')
            elif dependency == "winrt_base":
                lines.append('#include "../../../Engine/Platform/WindowsSDK/Creator.ComSupport.h"')
            elif dependency in MODULES:
                lines.append(f'#include "{dependency}.h"')
            else:
                raise ValueError(f"Unsupported adapter import: {line}")
        else:
            lines.append(re.sub(r"^export ", "", line))
    if name == "Creator.Core.Paths":
        # Source-only tools compile the same canonical definitions as inline code.
        # Native consumers import the module and link its implementation units.
        lines += ['#include "../../../Engine/Contracts/Platform/Windows/Creator.Core.Paths.WinAPI.cpp"',
                  '#include "../../../Engine/Utility_Framework/Runtime/PathFinder.cpp"']
    return "\n".join(lines).rstrip() + "\n"



def memory_source_inputs(paths):
    """Resolve the memory layer's source-only mirror after header retirement.

    The mimalloc implementation/header remains caller-owned: allocation probes
    either copy the real vendor tree or install their existing counting shim.
    """
    result = [Path(path) for path in paths]
    for stem in ("Ownership", "HeapAllocator", "Arena", "LifetimeToken"):
        name = "Creator.Core." + stem
        result += [Path(MODULE_SOURCES[name]), Path("Tools/build/ModuleAdapters") / (name + ".h")]
    result += [Path("Engine/Contracts/External/Creator.Ownership.h"),
               Path("Engine/Contracts/External/Creator.MemoryProvider.h"),
               Path("ThirdParty/ownership_cpp/include/own/ownership.hpp"),
               Path("Engine/Utility_Framework/Foundation/StandardLibrary.h")]
    if any(path.name in ("ThreadPool.cpp", "JobScheduler.cpp") for path in result):
        result = jobs_source_inputs(result)
    return list(dict.fromkeys(result))


def jobs_source_inputs(paths):
    """Source tools consume generated Jobs/JobGraph/Ownership adapters."""
    result = [Path(path) for path in paths]
    for name in ("Creator.Concurrency.Jobs", "Creator.Concurrency.JobGraph", "Creator.Core.Ownership"):
        result += [Path(MODULE_SOURCES[name]), Path("Tools/build/ModuleAdapters") / (name + ".h")]
    result += [Path("Engine/Contracts/External/Creator.EnkiTS.h"),
               Path("Engine/Contracts/External/Creator.Ownership.h"),
               Path("ThirdParty/ownership_cpp/include/own/ownership.hpp"),
               Path("Engine/Utility_Framework/Foundation/StandardLibrary.h")]
    return list(dict.fromkeys(result))


def generate(check=False):
    stale = []
    for name in MODULES:
        destination = ROOT / "Tools/build/ModuleAdapters" / f"{name}.h"
        expected = render(name)
        actual = destination.read_text(encoding="utf-8") if destination.exists() else None
        if actual != expected:
            if check:
                stale.append(str(destination.relative_to(ROOT)))
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(expected, encoding="utf-8", newline="\n")
    if stale:
        raise SystemExit("Stale module adapters; run py Tools/build/generate-module-adapters.py:\n" + "\n".join(stale))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    generate(parser.parse_args().check)
    print(f"PASS: {len(MODULES)} source-tool adapters match their module interfaces")
