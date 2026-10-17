"""Check owned C++ sources use the std module; does not invoke a compiler."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FORWARDERS = ROOT / "Tools/build/StdModuleHeaders"
STANDARD_HEADERS = set("""
algorithm any array atomic barrier bit bitset cassert cctype cerrno cfenv cfloat
charconv chrono cinttypes climits clocale cmath codecvt compare complex concepts
condition_variable coroutine csetjmp csignal cstdarg cstddef cstdint cstdio cstdlib
cstring ctime cuchar cwchar cwctype deque exception execution expected filesystem
flat_map flat_set format forward_list fstream functional future generator
initializer_list iomanip ios iosfwd iostream istream iterator latch limits list
locale map mdspan memory memory_resource mutex new numbers numeric optional
ostream print queue random ranges ratio regex scoped_allocator semaphore set
shared_mutex source_location span spanstream sstream stack stacktrace stdexcept
stdfloat stop_token streambuf string string_view strstream syncstream system_error
thread tuple type_traits typeindex typeinfo unordered_map unordered_set utility
valarray variant vector version
""".split())
ADAPTERS = {
    "Engine/Utility_Framework/StandardLibrary.h",
    "Engine/Utility_Framework/Contract.h",
}
EXCLUDED = re.compile(r"ThirdParty|External|external|vendor|imgui|ImGuizmo|plf_colony|/Saved/")


def main():
    tracked = subprocess.check_output(
        ["git", "ls-files", "--", "Engine", "Editor", "Player", "Tools", "Lattice", "Dynamic_CPP"],
        cwd=ROOT, text=True,
    ).splitlines()
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "--", "Engine", "Editor", "Player", "Tools", "Lattice"],
        cwd=ROOT, text=True,
    ).splitlines()
    failures = []
    checked = 0
    for name in sorted(set(tracked + untracked)):
        # git ls-files also reports tracked paths deleted in the working tree.
        if not (ROOT / name).is_file():
            continue
        name = name.replace("\\", "/")
        if name in ADAPTERS or EXCLUDED.search(name) or Path(name).suffix not in {".cpp", ".h", ".hpp", ".inl", ".ixx", ".cppm"}:
            continue
        checked += 1
        text = (ROOT / name).read_bytes().decode("latin1")
        for match in re.finditer(r'^\s*#\s*include\s*<([^>]+)>', text, re.M):
            if match[1] in STANDARD_HEADERS:
                failures.append(f"{name}: direct standard header <{match[1]}>")
    facade = (ROOT / "Engine/Utility_Framework/StandardLibrary.h").read_text(encoding="utf-8")
    if "import std;" not in facade or "CE_CONTRACT_STANDALONE" not in facade:
        failures.append("Standard-library module/parser adapter is missing")
    for path in FORWARDERS.iterdir():
        if path.is_file() and "Engine/Utility_Framework/StandardLibrary.h" not in path.read_text(encoding="utf-8"):
            failures.append(f"{path.name}: external include must forward to the module facade")
    if failures:
        raise SystemExit("\n".join(failures))
    forwarders = sum(path.is_file() for path in FORWARDERS.iterdir())
    print(f"PASS: {checked} owned C++ files; no direct STL headers outside explicit tool adapters; {forwarders} legacy include forwarders")


if __name__ == "__main__":
    main()
