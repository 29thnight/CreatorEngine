"""Check exact shadow-key append lambdas for byte parity, limits and allocation growth."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('extract', Path(__file__).with_name('verify-meshlet-preparation-cpu.py'))
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)

PROBE = r'''
#include "Engine/Containers/dynamic_array/dynamic_array.h"
#include <array>
#include <span>
#include <vector>
#include <iostream>
#include <stdexcept>
struct CountingAllocator : ce::heap_allocator
{
    static inline size_t allocations{};
    using byte_relocation_allocator = CountingAllocator;
    void* allocate(size_t bytes, size_t alignment)
    {
        ++allocations;
        return ce::heap_allocator::allocate(bytes, alignment);
    }
    void* reallocate(void* p, size_t oldBytes, size_t newBytes, size_t alignment)
    {
        ++allocations;
        return ce::heap_allocator::reallocate(p, oldBytes, newBytes, alignment);
    }
};
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
int main()
{
    try
    {
        ce::dynamic_array<std::byte, CountingAllocator> key;
        constexpr std::uint64_t maxKeyBytes = 64ull << 20;
        std::uint64_t keyBytes{};
        bool keyOverflow{};
        // PRODUCTION_LAMBDAS
        appendBytes({});
        Require(key.empty() && keyBytes == 0 && !keyOverflow, "empty span");
        std::array<std::byte, 64> input{};
        for (size_t i = 0; i < input.size(); ++i) input[i] = std::byte(i);
        std::vector<std::byte> expected;
        for (size_t i = 0; i < 1024; ++i)
        {
            const std::span<const std::byte> bytes(input.data(), 1 + i % input.size());
            appendBytes(bytes);
            expected.insert(expected.end(), bytes.begin(), bytes.end());
        }
        Require(key.size() == expected.size() && keyBytes == expected.size(), "key size/accounting");
        Require(memcmp(key.data(), expected.data(), expected.size()) == 0, "exact byte order");
        Require(CountingAllocator::allocations < 64, "append must have amortized allocation growth");
        const auto size = key.size();
        keyBytes = maxKeyBytes - input.size();
        appendBytes(input);
        Require(keyBytes == maxKeyBytes && !keyOverflow && key.size() == size + input.size(), "exact bound");
        appendBytes(std::span<const std::byte>(input.data(), 1));
        Require(keyOverflow && keyBytes == maxKeyBytes && key.size() == size + input.size(), "overflow rejects bytes");
        appendBytes({});
        Require(keyOverflow && key.size() == size + input.size(), "empty append preserves overflow");
        std::cout << "SHADOW_KEY_APPEND_OK allocations=" << CountingAllocator::allocations << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Release')
parser.add_argument('--source-ref')
parser.add_argument('--expect-failure', action='store_true')
args = parser.parse_args()
path = 'Engine/RenderEngine/MaterialGraphSceneHost.cpp'
source = (subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT).decode('utf-8-sig')
          if args.source_ref else (ROOT / path).read_text(encoding='utf-8-sig'))
lambdas = '\n'.join(extract.block(source, anchor, True)[0] for anchor in (
    'const auto accountBytes =', 'const auto appendBytes ='))
output = args.output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(PROBE.replace('// PRODUCTION_LAMBDAS', lambdas), encoding='utf-8')
flags = ['/Od', '/MDd', '/RTC1'] if args.configuration == 'Debug' else ['/O2', '/MD']
subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++latest', '/utf-8', *flags, '/I' + str(ROOT),
                '/Fo' + str(output.with_suffix('.obj')), '/Fe' + str(output.with_suffix('.exe')), str(output)],
               check=True, timeout=120)
result = subprocess.run([str(output.with_suffix('.exe'))], capture_output=True, text=True, timeout=30)
if args.expect_failure:
    assert result.returncode == 1 and 'amortized allocation growth' in result.stderr, result
else:
    assert result.returncode == 0, result.stderr
report = dict(passed=True, configuration=args.configuration, expectedFailure=args.expect_failure,
              sourceRef=args.source_ref, sourceTextSha256=hashlib.sha256(source.encode()).hexdigest(),
              stdout=result.stdout, stderr=result.stderr,
              scope='Production shadow-key accounting and append lambdas with real ce::dynamic_array; CPU only')
output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(result.stdout or result.stderr, end='')
