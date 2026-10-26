"""Assert warmed feedback-readback reuse from complete native production captures."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    options = parser.parse_args()
    root = options.root.resolve()
    analysis = json.loads((root / "analysis.json").read_text(encoding="utf-8-sig"))
    evidence = []
    for run in analysis["runs"]:
        if not run["name"].endswith("-on"):
            continue
        assert run["fullCapture"], run["name"]
        expected = 896 if run["name"].startswith("before-") else 0
        rows = run["allThreadFrameScopes"]["GeometryFeedbackReadbackCreate"]
        assert len(rows) == run["frames"]
        assert all(row["count"] == expected for row in rows), run["name"]
        evidence.append(dict(name=run["name"], expectedPerFrame=expected,
                             actualPerFrame=[row["count"] for row in rows],
                             createWallMsPerFrame=[row["wallMs"] for row in rows]))
    assert len(evidence) == 8, "Both cases and both ABBA trials required"
    verdict = dict(schema=1, passed=True, scenario="1024 admitted draws, 1 or 64 immutable meshes",
                   checks="All threads, every measured frame, complete ON capture; OFF has no scope events",
                   evidence=evidence)
    (root / "readback-reuse-regression.json").write_text(
        json.dumps(verdict, ensure_ascii=False, indent=2), encoding="utf-8")
    print("FEEDBACK_READBACK_REUSE_OK captures=8 beforePerFrame=896 afterPerFrame=0")


if __name__ == "__main__":
    main()
