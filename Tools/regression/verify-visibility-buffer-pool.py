"""Execute the production buffer Acquire/Return bodies against a tracked CPU device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('extract', Path(__file__).with_name('verify-meshlet-preparation-cpu.py'))
EXTRACT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACT)

PROBE = r'''
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
namespace ce
{
    template<std::size_t N> struct Name
    {
        char text[N];
        constexpr Name(const char (&value)[N]) { std::copy_n(value, N, text); }
    };
    template<Name> constexpr int marker() { return 0; }
    struct profile_scope { explicit profile_scope(int) {} };
}
struct RHIBufferHandle
{
    unsigned id{};
    bool IsValid() const { return id != 0; }
};
struct RHIBufferDesc
{
    std::uint64_t bytes{};
    bool allowUnorderedAccess{}, allowIndirectArguments{};
    const wchar_t* debugName{};
};
struct Device
{
    unsigned created{}, released{};
    std::unordered_set<unsigned> live;
    bool CreateBuffer(const RHIBufferDesc&, RHIBufferHandle& handle, std::string&)
    {
        handle.id = ++created;
        live.insert(handle.id);
        return true;
    }
    void ReleaseBuffer(RHIBufferHandle handle)
    {
        if (live.erase(handle.id) != 1) throw std::runtime_error("double or unknown release");
        ++released;
    }
};
bool VisibilityFail(std::string& error, const char* message) { error = message; return false; }
struct Pool
{
    struct Entry { RHIBufferHandle handle; std::uint64_t bytes; bool indirect; };
    Device& device;
    std::mutex mutex;
    std::vector<Entry> free;
    std::uint64_t cachedBytes{};
    bool closed{};
    explicit Pool(Device& value) : device(value) { free.reserve(64); }
    ~Pool() { for (const auto& entry : free) device.ReleaseBuffer(entry.handle); }
    // PRODUCTION_METHODS
};
unsigned checks{};
void Require(bool condition, const char* message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
int main()
{
    try
    {
        Device device;
        {
            Pool pool(device);
            std::string error;
            RHIBufferHandle handle;
            for (unsigned size = 1; size <= 64; ++size)
            {
                Require(pool.Acquire(size, false, handle, error), "initial acquire");
                pool.Return(handle, size, false, true);
            }
            Require(pool.free.size() == 64, "entry limit filled");
            Require(pool.Acquire(2048, false, handle, error), "new working set acquire");
            pool.Return(handle, 2048, false, true);
            const auto created = device.created;
            Require(pool.Acquire(2048, false, handle, error), "new working set reuse");
            Require(device.created == created, "stale full cache must admit and reuse new working set");
            pool.Return(handle, 2048, false, true);
            Require(pool.free.size() == 64 && !device.live.contains(1), "oldest unused entry evicted");
        }
        Require(device.live.empty(), "entry-limited pool releases every owned buffer");
        {
            Pool pool(device);
            std::string error;
            RHIBufferHandle first, second, active, replacement;
            constexpr std::uint64_t MiB = 1ull << 20;
            Require(pool.Acquire(16 * MiB, false, first, error), "first budget allocation");
            Require(pool.Acquire(16 * MiB, false, second, error), "second budget allocation");
            pool.Return(first, 16 * MiB, false, true);
            pool.Return(second, 16 * MiB, false, true);
            Require(pool.Acquire(4096, true, active, error), "active allocation");
            Require(pool.Acquire(20 * MiB, false, replacement, error), "budget replacement");
            pool.Return(replacement, 20 * MiB, false, true);
            Require(pool.cachedBytes == 20 * MiB && pool.free.size() == 1, "byte budget eviction");
            Require(device.live.contains(active.id), "never evict an in-use buffer");
            const auto created = device.created;
            Require(pool.Acquire(20 * MiB, false, replacement, error), "replacement reuse");
            Require(device.created == created, "replacement must be reused");
            pool.Return(replacement, 20 * MiB, false, true);
            pool.Return(active, 4096, true, false);
            Require(!device.live.contains(active.id), "uncertified return is never cached");
            RHIBufferHandle oversize;
            Require(pool.Acquire(33 * MiB, false, oversize, error), "oversize allocation");
            pool.Return(oversize, 33 * MiB, false, true);
            Require(!device.live.contains(oversize.id) && pool.cachedBytes == 20 * MiB,
                    "oversize return does not evict usable cache");
            pool.closed = true;
            Require(!pool.Acquire(16, false, oversize, error), "closed pool rejects acquire");
        }
        Require(device.live.empty(), "byte-limited pool releases every owned buffer");
        std::cout << "VISIBILITY_BUFFER_POOL_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "VISIBILITY_BUFFER_POOL_FAIL " << error.what() << '\n';
        return 1;
    }
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--source-ref', help='Extract a historical source for regression rejection checks')
parser.add_argument('--compiler', default='cl')
parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Release')
parser.add_argument('--expect-failure', action='store_true')
args = parser.parse_args()
args.output = args.output.resolve()
path = 'Engine/RenderEngine/GpuGeometryVisibility.cpp'
source = (subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT).decode('utf-8-sig')
          if args.source_ref else (ROOT / path).read_text(encoding='utf-8-sig'))
methods = '\n'.join(EXTRACT.block(source, anchor)[0] for anchor in (
    'bool Acquire(std::uint64_t bytes, bool indirect',
    'void Return(RHIBufferHandle handle, std::uint64_t bytes'))
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(PROBE.replace('// PRODUCTION_METHODS', methods), encoding='utf-8')
executable = args.output.with_suffix('.exe')
flags = ['/Od', '/MDd', '/RTC1'] if args.configuration == 'Debug' else ['/O2', '/MD']
subprocess.run([args.compiler, '/nologo', '/EHsc', '/std:c++latest', '/utf-8', *flags,
                '/Fo' + str(args.output.with_suffix('.obj')), '/Fe' + str(executable), str(args.output)],
               check=True, timeout=120)
result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=30)
if args.expect_failure:
    assert result.returncode == 1 and 'stale full cache must admit and reuse new working set' in result.stderr
else:
    assert result.returncode == 0, result.stderr
report = dict(passed=True, configuration=args.configuration, expectedFailure=args.expect_failure,
              sourceRef=args.source_ref, sourceSha256=hashlib.sha256(source.encode()).hexdigest(),
              stdout=result.stdout, stderr=result.stderr,
              scope='Exact production Acquire/Return; tracked CPU device, no GPU completion simulation')
args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(result.stdout or result.stderr, end='')
print('PRODUCTION_SHA256', hashlib.sha256(source.encode()).hexdigest())
print('SCOPE exact production Acquire/Return; tracked CPU device, no GPU completion simulation')
