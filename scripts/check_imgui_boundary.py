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

# ImGui 심볼. include 없이 이것만 쓰는 파일을 잡기 위한 패턴이다.
# ed:: 는 imgui-node-editor의 네임스페이스 별칭.
RE_SYMBOL = re.compile(
    r'\bImGui::|\bImGuiIO\b|\bImVec[24]\b|\bImDraw\w*|\bImFont\w*|'
    r'\bImGuiKey\w*|\bImGuiCol\w*|\bImGuiStyle\b|\bImTextureID\b|\bed::'
)

# 주석 제거용. 주석 처리된 ImGui 호출이 실제로 있어서(SSGIPass 등)
# 걸러내지 않으면 오탐이 난다.
RE_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
RE_LINE_COMMENT = re.compile(r'//[^\n]*')

# 심볼만 쓰고 include는 하지 않는 파일을 허용 목록에 적을 때 쓰는 표식.
# 실제 헤더 이름과 충돌하지 않도록 꺾쇠를 쓴다.
SYMBOL_SENTINEL = '<심볼-직접사용>'

# 빌드 산출물. Unity 빌드가 원본을 합쳐 놓은 것이라 세면 중복이 된다.
SKIP_PARTS = ('/x64/', '/bin/', '/obj/')


def strip_comments(text):
    """주석을 지운다. 주석 안의 ImGui 호출을 사용으로 세지 않기 위해서다."""
    text = RE_BLOCK_COMMENT.sub(' ', text)
    return RE_LINE_COMMENT.sub(' ', text)


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

                has_imgui_include = False
                for inc in RE_INCLUDE.findall(text):
                    if not is_imgui_header(inc, helper_headers):
                        continue
                    has_imgui_include = True
                    base = os.path.basename(inc.replace('\\', '/'))
                    if os.path.splitext(base)[0].lower() == own_header:
                        continue
                    violations.append((rel, base))

                # include 없이 심볼만 쓰는 파일.
                #
                # 이것이 없으면 게이트에 사각지대가 생긴다. 실제로 렌더 패스
                # 13종은 ImGui:: 를 쓰면서 include는 하지 않고, ReflectionFunction.h가
                # 리플렉션 매크로를 통해 뿌려 주는 <imgui.h>에 얹혀 컴파일된다.
                # 간선만 세면 이들이 보이지 않아, 이관 진척도 신규 위반도 놓친다.
                if not has_imgui_include and RE_SYMBOL.search(strip_comments(text)):
                    violations.append((rel, SYMBOL_SENTINEL))
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

    n_sym = sum(1 for _rel, base in current if base == SYMBOL_SENTINEL)
    n_inc = len(current) - n_sym
    print(f'런타임 -> ImGui 위반: {len(current)}건 '
          f'(include {n_inc} · 심볼 직접사용 {n_sym}) '
          f'/ 허용 목록 {len(allowed)}건')

    if stale:
        print(f'\n[정리 가능] 더는 존재하지 않는 허용 항목 {len(stale)}건 - '
              f'--update로 목록을 줄이세요:')
        for rel, base in stale:
            print(f'  {rel} -> {base}')

    if new_edges:
        print(f'\n[실패] 새 위반 {len(new_edges)}건:')
        for rel, base in new_edges:
            if base == SYMBOL_SENTINEL:
                print(f'  {rel} - ImGui 심볼을 쓰지만 include하지 않음')
            else:
                print(f'  {rel} -> {base}')
        print('\n런타임·플랫폼 계층은 ImGui에 의존할 수 없습니다.')
        print('에디터 UI는 EngineGUIWindow/ImGuiHelper 쪽에 두세요.')
        print('include 없이 심볼만 쓰는 것도 위반입니다 - 다른 헤더가 전이로 '
              '뿌려 주고 있을 뿐, 의존은 실재합니다.')
        return 1

    print('통과: 새 위반 없음')
    return 0


if __name__ == '__main__':
    sys.exit(main())
