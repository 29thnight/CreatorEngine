# -*- coding: utf-8 -*-
"""RenderEngine -> ScriptBinder include 경계 검사 (PHASE 4-2 CI 게이트).

규칙: RenderEngine/ 아래 소스는 ScriptBinder/ 헤더를 include할 수 없다.
기존 위반은 scripts/include_boundary_allowlist.txt 에 동결(래칫)돼 있고,
목록에 없는 새 위반이 생기면 실패한다. 절단이 진행되면 목록도 함께 줄인다.

사용법:
    python scripts/check_include_boundary.py            # 검사 (CI 게이트)
    python scripts/check_include_boundary.py --update   # 현재 상태로 허용 목록 재생성
"""
import os
import re
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, 'RenderEngine')
TGT_DIR = os.path.join(ROOT, 'ScriptBinder')
ALLOWLIST = os.path.join(ROOT, 'scripts', 'include_boundary_allowlist.txt')
HEADER_EXTS = ('.h', '.hpp', '.inl')
SOURCE_EXTS = ('.h', '.hpp', '.inl', '.cpp')
RE_INCLUDE = re.compile(r'^\s*#include\s*[<"]([^">]+)[">]', re.M)


def scan_violations():
    """(RenderEngine 파일, ScriptBinder 헤더) 위반 간선 목록을 돌려준다."""
    tgt_headers = {f for f in os.listdir(TGT_DIR) if f.endswith(HEADER_EXTS)}
    # 같은 이름이 RenderEngine에도 있으면 quote-include는 로컬이 우선이므로 제외
    local_headers = set()
    for dirpath, _dirs, files in os.walk(SRC_DIR):
        local_headers.update(f for f in files if f.endswith(HEADER_EXTS))
    ambiguous = tgt_headers & local_headers

    violations = []
    for dirpath, _dirs, files in os.walk(SRC_DIR):
        for f in files:
            if not f.endswith(SOURCE_EXTS):
                continue
            path = os.path.join(dirpath, f)
            try:
                with open(path, encoding='utf-8', errors='ignore') as fh:
                    text = fh.read()
            except OSError:
                continue
            rel = os.path.relpath(path, ROOT).replace('\\', '/')
            for inc in RE_INCLUDE.findall(text):
                base = os.path.basename(inc.replace('\\', '/'))
                if base in tgt_headers and base not in ambiguous:
                    violations.append((rel, base))
    return sorted(set(violations))


def load_allowlist():
    if not os.path.exists(ALLOWLIST):
        return set()
    entries = set()
    with open(ALLOWLIST, encoding='utf-8') as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = [p.strip() for p in line.split('->')]
            if len(parts) == 2:
                entries.add((parts[0], parts[1]))
    return entries


def write_allowlist(violations):
    with open(ALLOWLIST, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('# RenderEngine -> ScriptBinder include 경계 허용 목록 (PHASE 4-2 래칫)\n')
        fh.write('# 이 목록은 줄이기만 한다 — 새 간선 추가 금지.\n')
        fh.write('# 재생성: python scripts/check_include_boundary.py --update\n')
        for rel, base in violations:
            fh.write(f'{rel} -> {base}\n')


def main():
    violations = scan_violations()
    if '--update' in sys.argv:
        write_allowlist(violations)
        print(f'허용 목록 재생성: {len(violations)}개 간선 -> {ALLOWLIST}')
        return 0

    allowed = load_allowlist()
    current = set(violations)
    new_edges = sorted(current - allowed)
    stale = sorted(allowed - current)

    print(f'RenderEngine -> ScriptBinder include 간선: {len(current)}개 '
          f'(허용 목록 {len(allowed)}개)')
    if stale:
        print(f'\n[정리 가능] 더는 존재하지 않는 허용 항목 {len(stale)}개 — '
              f'--update로 목록을 줄이세요:')
        for rel, base in stale:
            print(f'  {rel} -> {base}')
    if new_edges:
        print(f'\n[실패] 새 include 간선 {len(new_edges)}개 — '
              f'RenderEngine은 ScriptBinder 헤더를 include할 수 없습니다:')
        for rel, base in new_edges:
            print(f'  {rel} -> {base}')
        return 1
    print('통과: 새 간선 없음')
    return 0


if __name__ == '__main__':
    sys.exit(main())
