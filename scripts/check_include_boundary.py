# -*- coding: utf-8 -*-
"""Editor/Core 물리 경계 래칫.

두 종류의 경계를 함께 검사한다.

1) 기존 include 층 행렬: 아래층이 위층 헤더를 include하지 못한다.
2) E0 물리 경계: vcxproj ProjectReference와 Editor 소스 편입, Core의
   ImGui/Editor include, EngineMode/BUILD_FLAG 잔여를 occurrence 래칫으로 막는다.

기존 부채는 두 allowlist에 수동 동결하며 목록은 줄이기만 한다. 특히 dirty
worktree에서 자동 baseline 갱신은 다른 작업의 중간 상태를 흡수하므로 금지한다.

include 층 배치(역사적 Core 내부 행렬):
    [6] EngineEntry · EngineGUIWindow · Player      실행 파일 · 에디터 셸
    [4] ScriptBinder                                게임플레이 · 씬 · 생명주기
    [3] RenderEngine                                렌더러 · 자원
    [2] ImGuiHelper · Physics                       외부 계통 래퍼
    [1] Utility_Framework                           코어

에디터 특권: EngineGUIWindow·EngineEntry는 최상층이라 무엇이든 include할 수
있다(에디터→엔진은 허용 방향). 단속 대상은 역방향(아래→위)뿐이다.

사용법:
    python scripts/check_include_boundary.py            # 검사 (CI 게이트)
    python scripts/check_include_boundary.py --update   # include allowlist만 재생성
    python scripts/check_include_boundary.py --print-layer-baseline
                                                       # 물리 부채를 stdout에만 출력
"""
import os
import re
import sys
from collections import Counter
import xml.etree.ElementTree as ET

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWLIST = os.path.join(ROOT, 'scripts', 'include_boundary_allowlist.txt')
LAYER_ALLOWLIST = os.path.join(ROOT, 'scripts', 'layer_boundary_allowlist.txt')
HEADER_EXTS = ('.h', '.hpp', '.inl')
SOURCE_EXTS = ('.h', '.hpp', '.inl', '.cpp')
RE_INCLUDE = re.compile(r'^\s*#include\s*[<"]([^">]+)[">]', re.M)
RE_ENGINE_MODE = re.compile(r'EngineMode::Is(Editor|Player)\s*\(')
RE_BUILD_FLAG = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*\bBUILD_FLAG\b', re.M)
RE_HOST_TEMP_POLICY = re.compile(r'\b(?:GetTempPathW|ResolveSystemTempDirectory)\s*\(')
RE_EDITOR_PLATFORM_API = re.compile(
    r'\b(?:ShellExecute(?:A|W)?|CreateProcessW|Show(?:Open|Save|Folder)FileDialog)\s*\(')
RE_EDITOR_ASSET_AUTHORING_IMPL = re.compile(
    r'\befsw::FileWatch(?:er|Listener)\b|'
    r'\b(?:CreateYamlMeta|ScanAndCleanupInvalidMeta|SaveExistVolumeProfile|'
    r'SaveMaterial|CreateVolumeProfile|ImportSourceAsset)\s*\(')
RE_DATA_SYSTEM_AUTHORING = re.compile(
    r'\b(?:copy_file|CopyHDRTexture|CopyTextureSelectType|SelectTextureType)\s*\(|'
    r'\bDataSystem::CopyTexture\s*\(|\bm_LoadTextureAssetQueue\b|'
    r'\bm_TargetTexturePath\b|TextureType Selector')
RE_LEGACY_ENGINE_SETTING = re.compile(
    r'\bEngineSetting(?:Instance|LaunchOptions)?\b|["<]EngineSetting\.h[">]')
RE_DEAD_TOOLCHAIN_SETTING = re.compile(
    r'\b(?:MSVCVersion|ExecuteVsWhere|GetMsbuildPath|GetMSVCVersion)\b')

# 프로젝트 이름 -> (소스 루트, 층 번호). 루트가 중첩된 프로젝트는 실제
# 소스가 있는 안쪽 폴더를 가리킨다.
#
# RenderEngine/Interfaces는 물리적으로 RenderEngine 안에 있지만 성격이
# 다르다 — PHASE 4가 세운 "공식 데이터 경계"(순수 데이터 헤더)의 집이라
# 모든 층이 include해도 되는 의사층 1이다. 검사에서 별도 프로젝트로 취급한다.
PROJECTS = {
    'Utility_Framework': ('Utility_Framework', 1),
    'RenderEngine.Interfaces': ('RenderEngine/Interfaces', 1),
    'ImGuiHelper': ('ImGuiHelper', 2),
    'Physics': ('Physics', 2),
    'RenderEngine': ('RenderEngine', 3),
    'ScriptBinder': ('ScriptBinder', 4),
    # E4-6b: ImGui 셸(IImGuiHost·DX12/Vulkan 구현)의 새 집. Core(3) 위의
    # presentation 층이라, Core가 이 헤더를 include하면 상향 간선으로 잡힌다.
    'HostImGuiPresentation': ('HostImGuiPresentation', 5),
    # E5: RHI self-test·benchmark 실행기의 새 집. Core 위의 developer-tools
    # 층 — Core가 테스트 헤더를 include하면 상향 간선으로 잡힌다.
    'RenderTests': ('RenderTests', 5),
    # E5-2(=E4-3 완결): 에디터 씬 오버레이 패스의 새 집(E6 목표 프로젝트 선행).
    'EditorRender': ('EditorRender', 5),
    'EngineEntry': ('EngineEntry', 6),
    'EngineGUIWindow': ('EngineGUIWindow', 6),
    'Player': ('Player', 6),
}

# 실제 MSBuild 프로젝트 그래프. ImGuiHelper는 presentation 층이므로 Runtime
# Core보다 위에 둔다. PROJECTS의 ImGuiHelper=2는 기존 include 행렬의 역사적
# 분류이고, E0부터는 아래의 물리 그래프가 Editor/Core 판정의 기준이다.
VCX_PROJECTS = {
    'Utility_Framework': ('Utility_Framework/Utility_Framework.vcxproj', 1),
    'Physics': ('Physics/Physics.vcxproj', 2),
    'RenderEngine': ('RenderEngine/RenderEngine.vcxproj', 3),
    'ScriptBinder': ('ScriptBinder/ScriptBinder.vcxproj', 4),
    'ImGuiHelper': ('ImGuiHelper/ImGuiHelper.vcxproj', 5),
    'HostImGuiPresentation':
        ('HostImGuiPresentation/HostImGuiPresentation.vcxproj', 5),
    'RenderTests': ('RenderTests/RenderTests.vcxproj', 5),
    'EditorRender': ('EditorRender/EditorRender.vcxproj', 5),
    'EditorRuntime': ('EngineEntry/EditorRuntime.vcxproj', 6),
    'EditorUI': ('EngineGUIWindow/EditorUI.vcxproj', 6),
    'Academy_4Q': ('Academy_4Q.vcxproj', 6),
    'Player': ('Player/Player.vcxproj', 6),
    'AssetPacker': ('Tools/AssetPacker/AssetPacker.vcxproj', 6),
}
CORE_PROJECTS = ('Utility_Framework', 'Physics', 'RenderEngine', 'ScriptBinder')
EDITOR_PATH_PARTS = {
    'editor', 'editorruntime', 'editorrender', 'editorui',
    'engineentry', 'engineguiwindow',
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


def xml_local_name(tag):
    return tag.rsplit('}', 1)[-1]


def normalized_rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def is_editor_path(value):
    norm = value.replace('\\', '/').lower()
    parts = [part for part in norm.split('/') if part not in ('.', '..')]
    base = os.path.basename(norm)
    return any(part in EDITOR_PATH_PARTS for part in parts) or 'editor' in base


def is_forbidden_core_include(value):
    norm = value.replace('\\', '/').lower()
    return 'imgui' in norm or 'node-editor' in norm or 'node_editor' in norm \
        or 'nodeeditor' in norm or is_editor_path(norm)


def scan_project_reference_debt(debt):
    by_absolute_path = {
        os.path.normcase(os.path.abspath(os.path.join(ROOT, rel_path))): name
        for name, (rel_path, _layer) in VCX_PROJECTS.items()
    }

    registered = set(by_absolute_path)
    discovered = set()
    for dirpath, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and d != 'vcpkg_installed']
        for filename in files:
            if filename.endswith('.vcxproj'):
                discovered.add(os.path.normcase(os.path.abspath(
                    os.path.join(dirpath, filename))))
    for path in sorted(discovered - registered):
        debt[('unregistered-project', normalized_rel(path), 'VCX_PROJECTS')] += 1

    for src_name, (rel_project, src_layer) in VCX_PROJECTS.items():
        project_path = os.path.join(ROOT, rel_project)
        try:
            root = ET.parse(project_path).getroot()
        except (OSError, ET.ParseError):
            debt[('project-parse', rel_project, 'valid vcxproj XML')] += 1
            continue

        for element in root.iter():
            if xml_local_name(element.tag) != 'ProjectReference':
                continue
            include = element.attrib.get('Include', '')
            target_path = os.path.normcase(os.path.abspath(os.path.join(
                os.path.dirname(project_path), include)))
            dst_name = by_absolute_path.get(target_path)
            if dst_name is None:
                debt[('unknown-project-reference', rel_project,
                      include.replace('\\', '/'))] += 1
                continue
            dst_layer = VCX_PROJECTS[dst_name][1]
            if dst_layer > src_layer:
                debt[('project-reference', rel_project,
                      VCX_PROJECTS[dst_name][0])] += 1


def scan_editor_source_membership(debt):
    source_exts = ('.h', '.hpp', '.inl', '.c', '.cc', '.cpp', '.cxx')
    for project_name in CORE_PROJECTS:
        rel_project = VCX_PROJECTS[project_name][0]
        project_path = os.path.join(ROOT, rel_project)
        try:
            root = ET.parse(project_path).getroot()
        except (OSError, ET.ParseError):
            continue
        for element in root.iter():
            item_kind = xml_local_name(element.tag)
            include = element.attrib.get('Include', '')
            if item_kind not in ('ClCompile', 'ClInclude', 'None') or not include:
                continue
            if item_kind == 'None' and not include.lower().endswith(source_exts):
                continue
            if is_editor_path(include):
                debt[('editor-source-membership', rel_project,
                      include.replace('\\', '/'))] += 1


def scan_core_source_debt(debt):
    for project_name in CORE_PROJECTS:
        rel_root = PROJECTS[project_name][0]
        project_root = os.path.join(ROOT, rel_root)
        for dirpath, filename in walk_sources(project_root):
            if not filename.endswith(SOURCE_EXTS):
                continue
            if project_of_dir(dirpath) != project_name:
                continue
            path = os.path.join(dirpath, filename)
            try:
                with open(path, encoding='utf-8', errors='ignore') as source_file:
                    text = source_file.read()
            except OSError:
                continue
            rel = normalized_rel(path)

            for include in RE_INCLUDE.findall(text):
                if is_forbidden_core_include(include):
                    debt[('forbidden-core-include', rel,
                          'Editor/ImGui include')] += 1

            for match in RE_ENGINE_MODE.finditer(text):
                debt[('engine-mode-branch', rel,
                      'EngineMode::Is' + match.group(1))] += 1

            build_flag_count = len(RE_BUILD_FLAG.findall(text))
            if build_flag_count:
                debt[('build-flag-conditional', rel, 'BUILD_FLAG')] += build_flag_count

            host_temp_count = len(RE_HOST_TEMP_POLICY.findall(text))
            if host_temp_count:
                debt[('host-temp-policy', rel,
                      'Host-owned temp root')] += host_temp_count

            # FileDialog.h declares the current platform primitive. E2 forbids Core
            # consumers, not the primitive's own implementation site.
            editor_platform_count = 0 if rel == 'Utility_Framework/FileDialog.h' \
                else len(RE_EDITOR_PLATFORM_API.findall(text))
            if editor_platform_count:
                debt[('editor-platform-api', rel,
                      'Editor-owned OS/file dialog API')] += editor_platform_count

            asset_authoring_count = len(RE_EDITOR_ASSET_AUTHORING_IMPL.findall(text))
            if asset_authoring_count:
                debt[('editor-asset-authoring-implementation', rel,
                      'EditorAssetDatabase-owned authoring API')] += asset_authoring_count

            if rel in ('RenderEngine/DataSystem.h', 'RenderEngine/DataSystem.cpp'):
                data_system_authoring_count = len(RE_DATA_SYSTEM_AUTHORING.findall(text))
                if data_system_authoring_count:
                    debt[('data-system-authoring-implementation', rel,
                          'EditorAssetDatabase/Presentation-owned import')] += \
                        data_system_authoring_count


def scan_removed_settings_debt(debt):
    """E1에서 폐기한 전역 facade와 부팅-time toolchain 탐색의 재유입을 막는다."""
    visited = set()
    for project_name, (rel_root, _layer) in PROJECTS.items():
        project_root = os.path.join(ROOT, rel_root)
        for dirpath, filename in walk_sources(project_root):
            if not filename.endswith(SOURCE_EXTS):
                continue
            path = os.path.normcase(os.path.abspath(os.path.join(dirpath, filename)))
            if path in visited:
                continue
            visited.add(path)
            try:
                with open(path, encoding='utf-8', errors='ignore') as source_file:
                    text = source_file.read()
            except OSError:
                continue
            rel = normalized_rel(path)
            legacy_count = len(RE_LEGACY_ENGINE_SETTING.findall(text))
            if legacy_count:
                debt[('legacy-settings-facade', rel, 'Runtime/Editor/Build settings')] \
                    += legacy_count
            toolchain_count = len(RE_DEAD_TOOLCHAIN_SETTING.findall(text))
            if toolchain_count:
                debt[('dead-toolchain-setting', rel, 'on-demand build orchestrator')] \
                    += toolchain_count


def scan_layer_debt():
    debt = Counter()
    scan_project_reference_debt(debt)
    scan_editor_source_membership(debt)
    scan_core_source_debt(debt)
    scan_removed_settings_debt(debt)
    return debt


def parse_count_allowlist(path):
    allowed = Counter()
    if not os.path.exists(path):
        return allowed
    with open(path, encoding='utf-8') as allow_file:
        for line_number, raw_line in enumerate(allow_file, 1):
            line = raw_line.strip()
            if not line or line.startswith('#'):
                continue
            fields = [field.strip() for field in line.split('|')]
            if len(fields) != 3 or '->' not in fields[1]:
                raise ValueError(
                    f'{normalized_rel(path)}:{line_number}: 잘못된 allowlist 형식')
            source, target = [field.strip() for field in fields[1].split('->', 1)]
            count = int(fields[2])
            if count <= 0:
                raise ValueError(
                    f'{normalized_rel(path)}:{line_number}: count는 양수여야 한다')
            allowed[(fields[0], source, target)] += count
    return allowed


def format_debt_entry(key, count):
    kind, source, target = key
    return f'{kind} | {source} -> {target} | {count}'


def print_layer_baseline(debt):
    print('# Editor/Core 물리 경계 기존 부채 래칫')
    print('# 형식: kind | source -> target | count')
    print('# 자동 갱신 금지. 감소한 항목만 검토 후 손으로 줄이거나 삭제한다.')
    for key in sorted(debt):
        print(format_debt_entry(key, debt[key]))


def check_layer_debt(debt):
    try:
        allowed = parse_count_allowlist(LAYER_ALLOWLIST)
    except (OSError, ValueError) as error:
        print(f'\n[실패] 물리 경계 allowlist를 읽지 못했습니다: {error}')
        return False

    excess = []
    stale = []
    for key in sorted(set(debt) | set(allowed)):
        current_count = debt[key]
        allowed_count = allowed[key]
        if current_count > allowed_count:
            excess.append((key, current_count, allowed_count))
        elif current_count < allowed_count:
            stale.append((key, current_count, allowed_count))

    print(f'물리 경계 부채: {sum(debt.values())}건 '
          f'(허용 {sum(allowed.values())}건, 항목 {len(debt)}개)')
    if stale:
        print(f'\n[실패] 감소한 물리 경계 항목 {len(stale)}개 — '
              '재증가 여지를 남기지 않도록 allowlist를 수동으로 줄이세요:')
        for key, current_count, allowed_count in stale:
            print(f'  {format_debt_entry(key, current_count)} '
                  f'(허용 {allowed_count})')
    if excess:
        print(f'\n[실패] 새 물리 경계 부채 {len(excess)}개:')
        for key, current_count, allowed_count in excess:
            print(f'  {format_debt_entry(key, current_count)} '
                  f'(허용 {allowed_count}, 초과 {current_count - allowed_count})')
    if stale or excess:
        return False
    print('통과: 새 프로젝트/소스/금지 의존 없음')
    return True


def main():
    violations = scan_violations()
    layer_debt = scan_layer_debt()
    if '--print-layer-baseline' in sys.argv:
        print_layer_baseline(layer_debt)
        return 0
    if '--update' in sys.argv:
        write_allowlist(violations)
        print(f'허용 목록 재생성: {len(violations)}개 간선 -> {ALLOWLIST}')
        print('주의: 물리 경계 allowlist는 갱신하지 않았습니다.')
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
    include_ok = not new_edges
    if new_edges:
        print(f'\n[실패] 새 상향 간선 {len(new_edges)}개 — '
              f'아래층은 위층 헤더를 include할 수 없습니다:')
        for rel, target in new_edges:
            print(f'  {rel} -> {target}')
    else:
        print('통과: 새 include 간선 없음')

    layer_ok = check_layer_debt(layer_debt)
    return 0 if include_ok and layer_ok else 1


if __name__ == '__main__':
    sys.exit(main())
