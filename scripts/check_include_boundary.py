# -*- coding: utf-8 -*-
"""층 행렬 include 경계 검사 (PHASE 4-2에서 출발, L0에서 전면 확장).

규칙: 화살표는 아래로만. 각 프로젝트에 층 번호를 주고, 자기보다 위층
프로젝트의 헤더를 include하면 위반이다(같은 층은 허용). 기존 위반은
scripts/include_boundary_allowlist.txt 에 동결(래칫)돼 있고, 목록에 없는
새 위반이 생기면 실패한다. 절단이 진행되면 목록도 함께 줄인다.

층 배치(EngineLayerSeparationPlan.md §2 / EnginePackagingPlan.md §3):
    [6] EngineEntry · EngineGUIWindow · TrainAsis   실행 파일 · 에디터 셸
    [4] ScriptBinder                                게임플레이 · 씬 · 생명주기
    [3] RenderEngine                                렌더러 · 자원
    [2] ImGuiHelper · Physics                       외부 계통 래퍼
    [1] Utility_Framework                           코어
    [0] SingletonManager · ManagedHeap              토대

에디터 특권: EngineGUIWindow·EngineEntry는 최상층이라 무엇이든 include할 수
있다(에디터→엔진은 허용 방향). 단속 대상은 역방향(아래→위)뿐이다.

사용법:
    python scripts/check_include_boundary.py            # 검사 (CI 게이트)
    python scripts/check_include_boundary.py --update   # 현재 상태로 허용 목록 재생성
"""
import os
import re
import sys
from collections import Counter

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWLIST = os.path.join(ROOT, 'scripts', 'include_boundary_allowlist.txt')
HEADER_EXTS = ('.h', '.hpp', '.inl')
SOURCE_EXTS = ('.h', '.hpp', '.inl', '.cpp')
RE_INCLUDE = re.compile(r'^\s*#include\s*[<"]([^">]+)[">]', re.M)

# 프로젝트 이름 -> (소스 루트, 층 번호). 루트가 중첩된 프로젝트는 실제
# 소스가 있는 안쪽 폴더를 가리킨다.
#
# RenderEngine/Interfaces는 물리적으로 RenderEngine 안에 있지만 성격이
# 다르다 — PHASE 4가 세운 "공식 데이터 경계"(순수 데이터 헤더)의 집이라
# 모든 층이 include해도 되는 의사층 1이다. 검사에서 별도 프로젝트로 취급한다.
PROJECTS = {
    'SingletonManager': ('SingletonManager/SingletonManager', 0),
    'ManagedHeap': ('ManagedHeap/ManagedHeap', 0),
    'Utility_Framework': ('Utility_Framework', 1),
    'RenderEngine.Interfaces': ('RenderEngine/Interfaces', 1),
    'ImGuiHelper': ('ImGuiHelper', 2),
    'Physics': ('Physics', 2),
    'RenderEngine': ('RenderEngine', 3),
    'ScriptBinder': ('ScriptBinder', 4),
    'EngineEntry': ('EngineEntry', 6),
    'EngineGUIWindow': ('EngineGUIWindow', 6),
    'TrainAsis': ('TrainAsis/TrainAsis', 6),
}

# 빌드 산출물 폴더는 걷지 않는다(유니티 블롭·중간 산출물).
SKIP_DIRS = {'x64', 'Debug', 'Release', 'GameBuild', 'obj', 'bin'}


def walk_sources(proj_root):
    for dirpath, dirs, files in os.walk(proj_root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            yield dirpath, f


def project_of_dir(dirpath):
    """절대 디렉터리 경로가 속한 프로젝트 이름. 가장 긴 루트가 이긴다
    (RenderEngine/Interfaces가 RenderEngine보다 먼저 매칭되도록)."""
    rel = os.path.relpath(dirpath, ROOT).replace('\\', '/')
    best, best_len = None, -1
    for name, (rel_root, _layer) in PROJECTS.items():
        if (rel == rel_root or rel.startswith(rel_root + '/')) \
                and len(rel_root) > best_len:
            best, best_len = name, len(rel_root)
    return best


def build_header_owners():
    """헤더 basename -> 소유 프로젝트 집합."""
    owners = {}
    for name, (rel_root, _layer) in PROJECTS.items():
        proj_root = os.path.join(ROOT, rel_root)
        for dirpath, f in walk_sources(proj_root):
            if f.endswith(HEADER_EXTS):
                owners.setdefault(f, set()).add(project_of_dir(dirpath))
    return owners


def resolve_owner(inc, src_proj, owners):
    """include 문자열의 소유 프로젝트를 판정한다. 판정 불가면 None.

    1) 경로에 프로젝트 폴더 이름이 있으면 그것이 답이다
       (예: "../Physics/ICollider.h").
    2) basename이 자기 프로젝트에 있으면 로컬 우선(quote-include 규칙) — 제외.
    3) basename 소유자가 정확히 하나면 그 프로젝트.
    4) 여럿이면 모호 — 판정하지 않는다(래칫이므로 놓침은 안전 측).
    """
    norm = inc.replace('\\', '/')
    dir_parts = norm.split('/')[:-1]
    # 긴 루트 우선 — "RenderEngine/Interfaces/…"가 RenderEngine으로 오인되지 않게.
    hinted, hinted_len = None, -1
    for name, (rel_root, _layer) in PROJECTS.items():
        root_parts = rel_root.split('/')
        for i in range(len(dir_parts) - len(root_parts) + 1):
            if dir_parts[i:i + len(root_parts)] == root_parts \
                    and len(rel_root) > hinted_len:
                hinted, hinted_len = name, len(rel_root)
    # 단일 폴더 이름만 있는 경우(예: "../Physics/…")도 같은 루프가 잡는다
    # (root_parts 길이 1). 중첩 프로젝트는 안쪽 폴더 이름으로도 힌트한다.
    if hinted is None:
        for part in dir_parts:
            for name, (rel_root, _layer) in PROJECTS.items():
                if rel_root.split('/')[-1] == part:
                    hinted = name
                    break
            if hinted:
                break
    if hinted:
        return hinted
    base = os.path.basename(norm)
    candidates = owners.get(base)
    if not candidates:
        return None
    if src_proj in candidates:
        return None
    if len(candidates) == 1:
        return next(iter(candidates))
    return None


def scan_violations():
    """(소스 상대경로, 대상 'Project/헤더') 상향 간선 목록을 돌려준다."""
    owners = build_header_owners()
    violations = []
    for src_proj, (rel_root, src_layer) in PROJECTS.items():
        proj_root = os.path.join(ROOT, rel_root)
        for dirpath, f in walk_sources(proj_root):
            if not f.endswith(SOURCE_EXTS):
                continue
            # 중첩 프로젝트(RenderEngine.Interfaces 등)의 파일은 바깥 프로젝트
            # 순회에서도 만난다 — 실소유 프로젝트 차례에서만 처리한다.
            if project_of_dir(dirpath) != src_proj:
                continue
            path = os.path.join(dirpath, f)
            try:
                with open(path, encoding='utf-8', errors='ignore') as fh:
                    text = fh.read()
            except OSError:
                continue
            rel = os.path.relpath(path, ROOT).replace('\\', '/')
            for inc in RE_INCLUDE.findall(text):
                dst_proj = resolve_owner(inc, src_proj, owners)
                if dst_proj is None or dst_proj == src_proj:
                    continue
                if PROJECTS[dst_proj][1] > src_layer:
                    base = os.path.basename(inc.replace('\\', '/'))
                    violations.append((rel, f'{dst_proj}/{base}'))
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
        fh.write('# 층 행렬 include 경계 허용 목록 (래칫)\n')
        fh.write('# 이 목록은 줄이기만 한다 — 새 간선 추가 금지.\n')
        fh.write('# 재생성: python scripts/check_include_boundary.py --update\n')
        for rel, target in violations:
            fh.write(f'{rel} -> {target}\n')


def pair_summary(violations):
    """(소스 프로젝트 -> 대상 프로젝트)별 간선 수."""
    counts = Counter()
    for rel, target in violations:
        src_proj = rel.split('/')[0]
        dst_proj = target.split('/')[0]
        counts[(src_proj, dst_proj)] += 1
    return counts


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

    print(f'상향(역방향) include 간선: {len(current)}개 (허용 목록 {len(allowed)}개)')
    for (src, dst), n in sorted(pair_summary(violations).items(),
                                key=lambda kv: -kv[1]):
        print(f'  {src} -> {dst}: {n}')
    if stale:
        print(f'\n[정리 가능] 더는 존재하지 않는 허용 항목 {len(stale)}개 — '
              f'--update로 목록을 줄이세요:')
        for rel, target in stale:
            print(f'  {rel} -> {target}')
    if new_edges:
        print(f'\n[실패] 새 상향 간선 {len(new_edges)}개 — '
              f'아래층은 위층 헤더를 include할 수 없습니다:')
        for rel, target in new_edges:
            print(f'  {rel} -> {target}')
        return 1
    print('통과: 새 간선 없음')
    return 0


if __name__ == '__main__':
    sys.exit(main())
