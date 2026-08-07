# -*- coding: utf-8 -*-
"""런타임·플랫폼 계층 -> ImGui include 경계 검사 (에디터 분리 래칫).

규칙: 런타임/플랫폼 계층(RenderEngine, EffectSystem, Utility_Framework,
ScriptBinder, EngineEntry)은 ImGui 헤더를 include할 수 없다. 에디터 UI는
EngineGUIWindow/ImGuiHelper 쪽에 있어야 한다.

왜 필요한가. check_include_boundary.py가 RenderEngine -> ScriptBinder 축을
154간선에서 92간선으로 줄이는 동안, ImGui 축은 2,053줄에서 2,055줄로
제자리였다. 게이트가 있는 축만 줄었다 — 측정되지 않는 경계는 지켜지지 않는다.

이 게이트는 이관을 대신하지 않는다. 이관이 끝나기 전에 역행부터 멈추는 것이
목적이다. 기존 위반은 scripts/imgui_boundary_allowlist.txt 에 동결(래칫)되고,
목록에 없는 새 위반이 생기면 실패한다. 이관이 진행되면 목록도 함께 줄인다.

사용법:
    python scripts/check_imgui_boundary.py            # 검사 (CI 게이트)
    python scripts/check_imgui_boundary.py --update   # 현재 상태로 허용 목록 재생성
"""
import os
import re
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 검사 대상: 런타임·플랫폼 계층. 에디터 프로젝트(EngineGUIWindow, ImGuiHelper)는
# ImGui를 쓰는 것이 정상이므로 제외한다.
SRC_DIRS = [
    'RenderEngine',
    'EffectSystem',
    'Utility_Framework',
    'ScriptBinder',
    'EngineEntry',
]

# 에디터 UI 헬퍼가 모여 있는 곳. 여기 헤더를 런타임이 include하면 위반이다.
EDITOR_HELPER_DIR = 'ImGuiHelper'

ALLOWLIST = os.path.join(ROOT, 'scripts', 'imgui_boundary_allowlist.txt')
SOURCE_EXTS = ('.h', '.hpp', '.inl', '.cpp')
RE_INCLUDE = re.compile(r'^\s*#include\s*[<"]([^">]+)[">]', re.M)

# 벤더링된 ImGui 계열 헤더. 경로가 어떻게 적히든 파일 이름으로 판정한다
# (예: "imgui-node-editor/imgui_node_editor.h" -> "imgui_node_editor.h").
RE_VENDOR_IMGUI = re.compile(r'^(imgui|imconfig|imstb_|imfilebrowser)', re.I)

# 빌드 산출물. Unity 빌드가 원본을 합쳐 놓은 것이라 세면 중복이 된다.
SKIP_PARTS = ('/x64/', '/bin/', '/obj/')


def is_imgui_header(include_path, helper_headers):
    """include 대상이 ImGui 계열 헤더인가."""
    base = os.path.basename(include_path.replace('\\', '/'))
    if RE_VENDOR_IMGUI.match(base):
        return True
    return base.lower() in helper_headers


def collect_helper_headers():
    """ImGuiHelper/ 아래 헤더 이름 집합 (소문자)."""
    helper_dir = os.path.join(ROOT, EDITOR_HELPER_DIR)
    if not os.path.isdir(helper_dir):
        return set()
    return {
        f.lower()
        for f in os.listdir(helper_dir)
        if f.endswith(('.h', '.hpp', '.inl'))
    }


def scan_violations():
    """(런타임 파일, ImGui 헤더) 위반 간선 목록을 돌려준다."""
    helper_headers = collect_helper_headers()

    violations = []
    for src in SRC_DIRS:
        src_dir = os.path.join(ROOT, src)
        if not os.path.isdir(src_dir):
            continue
        for dirpath, _dirs, files in os.walk(src_dir):
            for f in files:
                if not f.endswith(SOURCE_EXTS):
                    continue
                path = os.path.join(dirpath, f)
                rel = os.path.relpath(path, ROOT).replace('\\', '/')
                if any(p in '/' + rel.lower() for p in SKIP_PARTS):
                    continue
                try:
                    with open(path, encoding='utf-8', errors='ignore') as fh:
                        text = fh.read()
                except OSError:
                    continue
                # 자기 짝 헤더는 경계를 넘는 것이 아니다
                # (ImGuiRenderer.cpp -> ImGuiRenderer.h). 파일 자체가 잘못된
                # 계층에 있다는 사실은 다른 간선들이 이미 드러낸다.
                own_header = os.path.splitext(f)[0].lower()

                for inc in RE_INCLUDE.findall(text):
                    if not is_imgui_header(inc, helper_headers):
                        continue
                    base = os.path.basename(inc.replace('\\', '/'))
                    if os.path.splitext(base)[0].lower() == own_header:
                        continue
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
        fh.write('# 런타임·플랫폼 계층 -> ImGui include 허용 목록 (에디터 분리 래칫)\n')
        fh.write('# 이 목록은 줄이기만 한다 - 새 간선 추가 금지.\n')
        fh.write('# 재생성: python scripts/check_imgui_boundary.py --update\n')
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

    print(f'런타임 -> ImGui include 간선: {len(current)}개 '
          f'(허용 목록 {len(allowed)}개)')
    if stale:
        print(f'\n[정리 가능] 더는 존재하지 않는 허용 항목 {len(stale)}개 - '
              f'--update로 목록을 줄이세요:')
        for rel, base in stale:
            print(f'  {rel} -> {base}')
    if new_edges:
        print(f'\n[실패] 새 include 간선 {len(new_edges)}개 - '
              f'런타임·플랫폼 계층은 ImGui 헤더를 include할 수 없습니다:')
        for rel, base in new_edges:
            print(f'  {rel} -> {base}')
        print('\n에디터 UI는 EngineGUIWindow/ImGuiHelper 쪽에 두세요.')
        return 1
    print('통과: 새 간선 없음')
    return 0


if __name__ == '__main__':
    sys.exit(main())
