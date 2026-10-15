#!/usr/bin/env python3
"""Check documentation links, audit coverage, and reviewed source blobs.

Run from any directory: python docs/check_documentation.py
This read-only check needs no dependencies, network, or engine build.
Hashes establish integrity, not semantic correctness or runtime acceptance.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import sys
import urllib.parse

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"


def blob(path):
    data = path.read_bytes()
    header = b"blob " + str(len(data)).encode() + b"\0"
    return hashlib.sha1(header + data).hexdigest()


def document_anchors(path):
    text = path.read_text(errors="replace")
    result = set(re.findall(r"\bid=[\"']([^\"']+)", text))
    seen = collections.Counter()
    if path.suffix == ".md":
        clean = re.sub(r"```.*?```", "", text, flags=re.S)
        for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*$", clean, re.M):
            heading = re.sub(r"<[^>]+>", "", heading)
            heading = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", heading)
            heading = heading.strip().lower()
            heading = "".join(c for c in heading if c.isalnum() or c in "_- ")
            heading = heading.replace(" ", "-")
            count = seen[heading]
            seen[heading] += 1
            result.add(heading + (f"-{count}" if count else ""))
    return result


def check_links(issue):
    links = anchors = 0
    cache = {}
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in {".md", ".html", ".jsx"} or not path.is_file():
            continue
        parts = path.relative_to(ROOT).parts
        if any(part in {".git", "node_modules", "dist", "ThirdParty"} for part in parts):
            continue
        text = re.sub(r"```.*?```", "", path.read_text(errors="replace"), flags=re.S)
        refs = re.findall(r"\[[^\]]*\]\(([^)]+)\)", text)
        refs += re.findall(r"(?:href|src)=[\"']([^\"']+)", text)
        for url in refs:
            url = url.strip().split(' "')[0].strip("<>")
            parsed = urllib.parse.urlsplit(url)
            if parsed.scheme or url.startswith("//") or "{" in url or not url:
                continue
            # React API hashes are data routes rather than static DOM sections.
            if path.suffix == ".jsx" and parsed.fragment:
                continue
            # JSX literal links resolve against the served HTML, not docs/src.
            base = DOCS if path.suffix == ".jsx" else path.parent
            target = (base / urllib.parse.unquote(parsed.path)).resolve() if parsed.path else path
            if not path.is_relative_to(DOCS) and not target.is_relative_to(DOCS):
                continue
            if not target.is_relative_to(ROOT):
                issue(path, url, "outside-repository")
                continue
            if not target.exists():
                issue(path, url, "missing-file")
                continue
            links += 1
            if not parsed.fragment or target.is_dir() or target.suffix not in {".md", ".html"}:
                continue
            if target not in cache:
                cache[target] = document_anchors(target)
            anchors += 1
            if urllib.parse.unquote(parsed.fragment) not in cache[target]:
                issue(path, url, "missing-anchor")
    return links, anchors


def api_anchors(page):
    anchors = ["declaration"] if page.get("signature") else []
    if page.get("note"):
        anchors.append("contract")
    anchors += [section["id"] for section in page.get("sections", [])]
    if page.get("example"):
        anchors.append("example")
    if page.get("members") is not None:
        anchors.append("members")
        for member in page["members"]:
            slug = re.sub(r"[^a-zA-Z0-9_-]+", "-", member[0]).rstrip("-").lower()
            anchors.append("member-" + slug)
    return anchors


def check_api(issue):
    path = DOCS / "api/reference.json"
    api = json.loads(path.read_text())
    page_ids = [page["id"] for page in api["pages"]]
    if len(page_ids) != len(set(page_ids)):
        issue(path, "", "duplicate-api-page")
    for page in api["pages"]:
        if not (ROOT / page["source"]).is_file():
            issue(path, page["source"], "missing-api-source")
        for card in page.get("cards", []):
            if card[2] not in page_ids:
                issue(path, card[2], "missing-api-page")
        anchors = api_anchors(page)
        for old, target in page.get("legacyAnchors", {}).items():
            if target not in anchors:
                issue(path, page["id"] + "~" + old, "missing-compatibility-target")
        for anchor, count in collections.Counter(anchors).items():
            if count > 1:
                issue(path, page["id"] + "~" + anchor, "duplicate-api-anchor")
    app_path = DOCS / "src/App.jsx"
    routes = re.findall(r"['\"]((?:guide|script|native|tools)/[^'\" ]+)['\"]", app_path.read_text())
    for route in routes:
        if route not in page_ids:
            issue(app_path, route, "missing-app-route")
    for shot in api["meta"].get("screenshots", []):
        target = ROOT / shot["path"]
        if not target.is_file() or blob(target) != shot["blob"]:
            issue(path, shot["path"], "historical-screenshot-drift")
    for source, expected in api["meta"].get("sourceFiles", {}).items():
        target = ROOT / source
        if not target.is_file() or blob(target) != expected:
            issue(path, source, "api-source-hash-drift")


def check_coverage(issue):
    path = DOCS / "DocumentationCoverage.json"
    if not path.exists():
        issue(path, "", "missing-coverage")
        return
    coverage = json.loads(path.read_text())
    seen = set()
    for item in coverage["files"]:
        name = item["path"]
        target = ROOT / name
        if name in seen:
            issue(path, name, "duplicate-coverage")
        seen.add(name)
        if not item.get("key_verified_claims") or not item.get("limitations"):
            issue(target, "", "missing-review-disposition")
        changed = item["baseline_blob"] != item["result_blob"]
        if item["disposition"].startswith(("preserved", "retained")) and changed:
            issue(target, "", "preservation-claim-mismatch")
        if target.suffix in {".md", ".html"} and not changed:
            issue(target, "", "prose-not-replaced")
        if not target.is_file() or blob(target) != item["result_blob"]:
            issue(path, name, "document-hash-drift")
        for source in item.get("sources", []):
            evidence = ROOT / source["path"]
            if not evidence.exists():
                issue(target, source["path"], "missing-evidence")
            elif evidence.is_file() and blob(evidence) != source["blob"]:
                issue(target, source["path"], "evidence-hash-drift")
    api = json.loads((DOCS / "api/reference.json").read_text())
    expected_pages = {page["id"] for page in api["pages"]}
    reviewed_pages = [page["page_id"] for page in coverage.get("api_pages", [])]
    if set(reviewed_pages) != expected_pages or len(reviewed_pages) != len(expected_pages):
        issue(path, "", "api-review-coverage-mismatch")
    for page in coverage.get("api_pages", []):
        for source in page.get("sources", []):
            evidence = ROOT / source["path"]
            if not evidence.is_file() or blob(evidence) != source["blob"]:
                issue(path, source["path"], "api-evidence-hash-drift")
    actual = {str(p.relative_to(ROOT)) for p in DOCS.rglob("*") if p.is_file()}
    expected = seen | set(coverage["additional_audit_files"])
    for name in sorted(actual ^ expected):
        issue(ROOT / name, "", "coverage-file-set-mismatch")
    if len(seen) != coverage["baseline_file_count"]:
        issue(path, "", "coverage-count-mismatch")


def main():
    issues = []

    def issue(path, url, kind):
        issues.append(dict(file=str(path.relative_to(ROOT)), url=url, kind=kind))

    links, anchors = check_links(issue)
    check_api(issue)
    check_coverage(issue)
    report = dict(
        files=sum(path.is_file() for path in DOCS.rglob("*")),
        links_checked=links,
        anchors_checked=anchors,
        issues=issues,
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
