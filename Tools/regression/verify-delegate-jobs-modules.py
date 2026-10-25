"""Keep Delegate/Jobs definitions in modules and native consumers on direct imports."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RULES = {
    "Creator.Core.Delegate": rb"\bce::delegate(?:_handle)?\b|(?:\.|->)(?:add_lambda|add_shared|add_raw|broadcast)\s*\(",
    "Creator.Concurrency.Jobs": rb"\b(?:job_scheduler|job_group|job_handle|thread_pool|get_job_scheduler|get_thread_pool)\b",
}

def main():
    failures = []
    counts = dict.fromkeys(RULES, 0)
    for name in ("Delegates/Delegate.h", "Delegates/Delegate.inl", "Concurrency/ThreadPool.h", "Concurrency/JobScheduler.h"):
        if (ROOT / "Engine/Utility_Framework" / name).exists():
            failures.append(f"retired definition exists: {name}")
    paths = subprocess.check_output(
        ["git", "ls-files", "-co", "--exclude-standard", "--", "Engine", "Editor", "Player", "HeadlessHost"],
        cwd=ROOT, text=True).splitlines()
    for name in sorted(set(paths)):
        path = ROOT / name
        if path.suffix not in (".h", ".hpp", ".cpp", ".inl") or not path.is_file() or name.startswith("Engine/Contracts/"):
            continue
        code = path.read_bytes()
        body = re.sub(rb"/\*.*?\*/|//[^\r\n]*", b"", code, flags=re.S)
        body = re.sub(rb"^[ \t]*(?:#include|import)[^\r\n]*", b"", body, flags=re.M)
        for module, pattern in RULES.items():
            if re.search(pattern, body):
                counts[module] += 1
                if f"import {module};".encode() not in code and f"module {module};".encode() not in code:
                    failures.append(f"{name}: directly import {module}")
        if re.search(rb"\bCore::Delegate(?:Handle)?\b", body):
            failures.append(f"{name}: legacy delegate naming")
        if re.search(rb'#include\s*[<"]enkiTS/', code):
            failures.append(f"{name}: enkiTS include must stay in the private provider")
    if failures:
        raise SystemExit("\n".join(failures))
    print("DELEGATE_JOBS_BOUNDARY_OK", counts)

if __name__ == "__main__":
    main()
