# reflgen 전환 codemod (reflgen 도입 P4) — static consteval auto reflect() 레시피를 [[reflgen::…]] attribute 로 옮긴다.
#
#   python Tools/migration/reflgen_codemod.py            # 무엇을 바꿀지 보고만 한다
#   python Tools/migration/reflgen_codemod.py --apply    # header 를 고치고 동등성 증명 표를 쓴다
#
# 레시피는 이 타입이 직렬화·인스펙터에 내놓는 필드를 적은 목록(opt-in)이고, reflgen 은 모든 비정적 데이터 멤버를
# 반영한다(opt-out). 그래서 레시피에 없던 멤버에는 [[reflgen::ignore]] 를 단다 — 저장되지 않는 런타임 상태라는
# 표지가 멤버마다 남는다. 레시피 순서는 모든 타입에서 선언 순서와 같다(실측) — YAML 키 순서는 바뀌지 않는다.
#
# 옮기기 전의 레시피는 모듈마다 동등성 증명 표(ReflgenParity.cpp)로 남긴다. 다리가 만든 엔진 스키마가 그 표와
# 필드 이름·순서·속성 타입·속성 값·메서드·파라미터 이름까지 같은지 컴파일 때 단정한다 — 소비자는 스키마 타입에
# 대한 template 이라 같으면 동작도 같다. 표는 이 폴더의 ReflgenParity.h 를 include 한다. 새 표는 그 모듈의
# vcxproj 에 ClCompile 로 넣고, 증명한 뒤에는 지운다(필드가 바뀌면 깨진다 — 넣는 커밋과 지우는 커밋을 가른다).
#
# 위치는 libclang(C API, ctypes — Visual Studio 의 LLVM)으로 얻는다. 파싱 인자는 빌드가 남긴 reflgen 인자 파일
# (Build/Obj/*/x64-Debug/reflgen/reflgen_*.args)을 합쳐 쓴다 — Debug x64 를 한 번 빌드한 뒤에 돌린다. master 를
# 다시 받아 레시피가 바뀌었으면 이 도구를 다시 돌린다 — 레시피가 남은 타입만 옮기고, 증명 표에서는 그 타입의 줄만
# 바꾼다(이미 옮긴 타입의 줄은 둔다).
import argparse
import ctypes as C
import os
import pathlib
import re
import sys

LLVM = pathlib.Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64')
ROOT = pathlib.Path(__file__).resolve().parents[2]
# meta 코어의 selftest 카나리아는 meta 로 둔다 — meta 코어를 지키는 것이 그것들의 일이다.
SKIP_NAMESPACES = ('meta::detail::selftest',)
# 모듈(vcxproj 이름) → 동등성 증명 표를 둘 자리.
PARITY_FILES = {
    'RenderEngine': 'Engine/RenderEngine/ReflgenParity.cpp',
    'SceneRuntime': 'Engine/SceneRuntime/ReflgenParity.cpp',
    'Editor': 'Editor/EngineGUIWindow/ReflgenParity.cpp',
}
# 레시피 속성 → reflgen attribute. 엔진에만 있는 속성은 creator 이름공간(ReflgenBridge.h)이다.
ATTRIBUTES = {
    'range': ('reflgen::range', True),
    'displayName': ('reflgen::display_name', True),
    'hidden': ('reflgen::hidden', False),
    'readonly': ('reflgen::readonly', False),
    'debugOnly': ('creator::debug_only', False),
    'wide': ('creator::wide', False),
    'units': ('creator::units', True),
}
# 레시피의 메서드 수식 → reflgen 메서드 attribute(ReflgenBridge.h 가 엔진 메서드의 인스펙터 플래그로 옮긴다).
METHOD_FLAGS = {
    'readOnlyInInspector': 'creator::read_only_in_inspector',
    'hideInInspector': 'creator::hide_in_inspector',
}

# ── libclang ────────────────────────────────────────────────────────────────

lib = C.CDLL(str(LLVM / 'bin' / 'libclang.dll'))


class CXString(C.Structure):
    _fields_ = [('data', C.c_void_p), ('flags', C.c_uint)]


class CXCursor(C.Structure):
    _fields_ = [('kind', C.c_int), ('xdata', C.c_int), ('data', C.c_void_p * 3)]


class CXSourceLocation(C.Structure):
    _fields_ = [('ptr_data', C.c_void_p * 2), ('int_data', C.c_uint)]


class CXSourceRange(C.Structure):
    _fields_ = [('ptr_data', C.c_void_p * 2), ('begin', C.c_uint), ('end', C.c_uint)]


VISITOR = C.CFUNCTYPE(C.c_int, CXCursor, CXCursor, C.c_void_p)


def fn(name, restype, *argtypes):
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = list(argtypes)
    return f


createIndex = fn('clang_createIndex', C.c_void_p, C.c_int, C.c_int)
parse = fn('clang_parseTranslationUnit', C.c_void_p, C.c_void_p, C.c_char_p, C.POINTER(C.c_char_p), C.c_int,
           C.c_void_p, C.c_uint, C.c_uint)
tuCursor = fn('clang_getTranslationUnitCursor', CXCursor, C.c_void_p)
visitChildren = fn('clang_visitChildren', C.c_uint, CXCursor, VISITOR, C.c_void_p)
getKind = fn('clang_getCursorKind', C.c_int, CXCursor)
getSpelling = fn('clang_getCursorSpelling', CXString, CXCursor)
getCString = fn('clang_getCString', C.c_char_p, CXString)
disposeString = fn('clang_disposeString', None, CXString)
getLocation = fn('clang_getCursorLocation', CXSourceLocation, CXCursor)
getExtent = fn('clang_getCursorExtent', CXSourceRange, CXCursor)
rangeStart = fn('clang_getRangeStart', CXSourceLocation, CXSourceRange)
rangeEnd = fn('clang_getRangeEnd', CXSourceLocation, CXSourceRange)
fileLocation = fn('clang_getFileLocation', None, CXSourceLocation, C.POINTER(C.c_void_p), C.POINTER(C.c_uint),
                  C.POINTER(C.c_uint), C.POINTER(C.c_uint))
fileName = fn('clang_getFileName', CXString, C.c_void_p)
isStatic = fn('clang_CXXMethod_isStatic', C.c_uint, CXCursor)
access = fn('clang_getCXXAccessSpecifier', C.c_int, CXCursor)
isDefinition = fn('clang_isCursorDefinition', C.c_uint, CXCursor)
isMacroExpansion = fn('clang_Location_isFromMainFile', C.c_int, CXSourceLocation)

K_STRUCT, K_CLASS, K_FIELD, K_METHOD, K_NAMESPACE = 2, 4, 6, 21, 22


def text_of(s):
    value = getCString(s)
    disposeString(s)
    return value.decode('utf-8', 'replace') if value else ''


def where(location):
    """파일(정규화)·줄·offset — 매크로를 편 자리가 아니라 파일 안 자리."""
    f = C.c_void_p()
    line = C.c_uint()
    column = C.c_uint()
    offset = C.c_uint()
    fileLocation(location, C.byref(f), C.byref(line), C.byref(column), C.byref(offset))
    name = text_of(fileName(f)) if f.value else ''
    return name.replace('\\', '/'), line.value, offset.value


def children(cursor):
    result = []

    @VISITOR
    def visit(child, parent, data):
        result.append(CXCursor(child.kind, child.xdata, (C.c_void_p * 3)(*child.data)))
        return 1

    visitChildren(cursor, visit, None)
    return result


def parse_engine(headers):
    args = []
    for path in sorted((ROOT / 'Build' / 'Obj').glob('*/x64-Debug/reflgen/reflgen_*.args')):
        for line in path.read_text(encoding='utf-8').splitlines():
            if line and not line.startswith('-std=') and line not in args:
                args.append(line)
    if not args:
        raise SystemExit('reflgen 인자 파일이 없다 — Debug x64 를 한 번 빌드한 뒤에 돌린다')
    resource = LLVM / 'lib' / 'clang'
    resource = sorted(resource.iterdir())[-1]
    args = ['-x', 'c++', '-std=c++23', '-ferror-limit=0', '-fbracket-depth=4096', '-Wno-unknown-attributes',
            '-resource-dir', str(resource), '-isystem', str(resource / 'include')] + args
    main_file = ROOT / 'Build' / 'reflgen_codemod_main.cpp'
    main_file.write_text(''.join(f'#include "{p.as_posix()}"\n' for p in headers), encoding='utf-8')
    argv = [a.encode('utf-8') for a in args]
    tu = parse(createIndex(0, 0), str(main_file).encode('utf-8'), (C.c_char_p * len(argv))(*argv), len(argv),
               None, 0, 0)
    if not tu:
        raise SystemExit('libclang 이 header 를 읽지 못했다')
    return tu


# ── 레시피 읽기 ──────────────────────────────────────────────────────────────

def split_top_level(text, separator=','):
    parts, depth, current, quote = [], 0, '', None
    for ch in text:
        if quote:
            current += ch
            if ch == quote:
                quote = None
            continue
        if ch in '"\'':
            quote = ch
        elif ch in '([{<':
            depth += 1
        elif ch in ')]}>':
            depth -= 1
        if ch == separator and depth == 0:
            parts.append(current.strip())
            current = ''
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def method_calls(chain):
    """meta::method<&T::f> 뒤의 .이름(인자) 사슬 — [(이름, 인자 글자)]. 인자 안의 괄호·문자열을 건넌다."""
    calls, i = [], 0
    while chain[i:].strip():
        m = re.match(r'\s*\.(\w+)\(', chain[i:])
        if not m:
            raise SystemExit(f'읽지 못한 메서드 수식: {chain}')
        start = j = i + m.end()
        depth, quote = 1, None
        while depth:
            ch = chain[j]
            if quote:
                quote = None if ch == quote else quote
            elif ch in '"\'':
                quote = ch
            elif ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            j += 1
        calls.append((m.group(1), chain[start:j - 1]))
        i = j
    return calls


def recipe_entries(body):
    """meta::schema<Self>( … ) 의 항목 — ('field', 이름, [(속성, 인자)]) 또는 ('method', 이름, ([파라미터], [수식])).
    레시피 안의 주석은 읽을 때 걷는다(옮길 주석은 main 이 따로 알린다 — 사람이 멤버 옆으로 옮긴다)."""
    body = re.sub(r'//[^\n]*', '', body)
    start = body.index('meta::schema<')
    open_paren = body.index('(', body.index('>', start))
    depth = 0
    for i in range(open_paren, len(body)):
        depth += body[i] == '('
        depth -= body[i] == ')'
        if depth == 0:
            inner = body[open_paren + 1:i]
            break
    entries = []
    for item in split_top_level(inner):
        item = ' '.join(item.split())
        m = re.match(r'meta::field<&\w+::(\w+)>(.*)$', item)
        if m:
            attributes = []
            with_match = re.match(r'\s*\.with\((.*)\)\s*$', m.group(2))
            if with_match:
                for attribute in split_top_level(with_match.group(1)):
                    a = re.match(r'meta::(\w+)\((.*)\)$', attribute)
                    if not a or a.group(1) not in ATTRIBUTES:
                        raise SystemExit(f'옮기지 못하는 속성: {attribute}')
                    attributes.append((a.group(1), a.group(2).strip()))
            elif m.group(2).strip():
                raise SystemExit(f'읽지 못한 필드 항목: {item}')
            entries.append(('field', m.group(1), attributes))
            continue
        m = re.match(r'meta::method<&\w+::(\w+)>(.*)$', item)
        if m:
            parameters, flags = [], []
            for call, argument in method_calls(m.group(2)):
                if call == 'params':
                    parameters = [p.strip() for p in split_top_level(argument)]
                elif call in METHOD_FLAGS and not argument.strip():
                    flags.append(call)
                else:
                    raise SystemExit(f'옮기지 못하는 메서드 수식: {item}')
            entries.append(('method', m.group(1), (parameters, flags)))
            continue
        raise SystemExit(f'읽지 못한 레시피 항목: {item}')
    return entries


def reflgen_attributes(attributes):
    written = []
    for name, argument in attributes:
        spelling, has_arguments = ATTRIBUTES[name]
        written.append(f'{spelling}({argument})' if has_arguments else spelling)
    return written


# ── 수집 ────────────────────────────────────────────────────────────────────

class Edit:
    def __init__(self, offset, text, remove=0):
        self.offset, self.text, self.remove = offset, text, remove


def collect(tu, targets):
    wanted = {p.as_posix().lower() for p in targets}
    plans = []

    def walk(cursor, scope):
        for child in children(cursor):
            kind = getKind(child)
            if kind == K_NAMESPACE:
                walk(child, scope + [text_of(getSpelling(child))])
            elif kind in (K_STRUCT, K_CLASS) and isDefinition(child):
                name = text_of(getSpelling(child))
                file = where(getLocation(child))[0]
                qualified = '::'.join(scope + [name])
                if file.lower() in wanted and not qualified.startswith(SKIP_NAMESPACES):
                    plan = plan_for(child, qualified, file)
                    if plan:
                        plans.append(plan)
                walk(child, scope + [name])

    walk(tuCursor(tu), [])
    return plans


def plan_for(record, qualified, file):
    """편집은 바이트 위치로 한다 — libclang 의 offset 이 바이트이고, 엔진 header 가 모두 UTF-8 인 것은 아니다(CP949
    주석이 남은 파일이 있다). 넣는 글자는 모두 ASCII 라 파일의 인코딩·줄바꿈·BOM 을 건드리지 않는다."""
    members = children(record)
    recipes = [m for m in members if getKind(m) == K_METHOD and text_of(getSpelling(m)) == 'reflect' and isStatic(m)]
    if not recipes:
        return None
    raw = pathlib.Path(file).read_bytes()
    newline = b'\r\n' if b'\r\n' in raw else b'\n'

    def span(begin, end):
        return raw[begin:end].decode('utf-8', 'replace')

    extent = getExtent(recipes[0])
    recipe_begin = where(rangeStart(extent))[2]
    recipe_end = where(rangeEnd(extent))[2]
    entries = recipe_entries(span(recipe_begin, recipe_end))
    listed_fields = {e[1]: e[2] for e in entries if e[0] == 'field'}
    listed_methods = {e[1]: e[2] for e in entries if e[0] == 'method'}

    edits = []
    # 레시피를 줄째로 걷는다(앞 들여쓰기부터 뒤 줄바꿈까지).
    line_begin = raw.rfind(b'\n', 0, recipe_begin) + 1
    line_end = raw.find(b'\n', recipe_end)
    line_end = len(raw) if line_end < 0 else line_end + 1
    # 같은 줄의 다른 코드를 함께 지우지 않는다 — 그런 레시피는 손으로 옮긴다.
    before = span(line_begin, recipe_begin).strip().lstrip('\ufeff')
    after = span(recipe_end, line_end).strip()
    if before or (after and not after.startswith('//')):
        raise SystemExit(f'{qualified}: 레시피와 같은 줄에 다른 코드가 있다 — 손으로 옮긴다')
    edits.append(Edit(line_begin, b'', line_end - line_begin))

    # 클래스 이름 앞에 [[reflgen::reflect]].
    name_offset = where(getLocation(record))[2]
    edits.append(Edit(name_offset, b'[[reflgen::reflect]] '))

    # 필드 — 같은 선언의 선언자들은 범위 시작이 같다.
    fields = [m for m in members if getKind(m) == K_FIELD]
    declarations = {}
    for field in fields:
        declarations.setdefault(where(rangeStart(getExtent(field)))[2], []).append(field)
    needs_friend = False
    seen_fields = set()
    for start, group in declarations.items():
        names = [text_of(getSpelling(f)) for f in group]
        seen_fields.update(names)
        written = []
        for f, n in zip(group, names):
            if n in listed_fields:
                written.append(reflgen_attributes(listed_fields[n]))
                if access(f) != 1:
                    needs_friend = True
            else:
                written.append(['reflgen::ignore'])
        if len(group) == 1 or all(w == written[0] for w in written):
            if written[0]:
                edits.append(Edit(start, f'[[{", ".join(written[0])}]] '.encode()))
        else:
            # 선언자마다 다르면 선언자 뒤에 단다 — 그 선언자에만 붙는다.
            for f, w in zip(group, written):
                if w:
                    after_name = where(getLocation(f))[2] + len(text_of(getSpelling(f)).encode())
                    edits.append(Edit(after_name, f' [[{", ".join(w)}]]'.encode()))
    missing = [n for n in listed_fields if n not in seen_fields]
    if missing:
        raise SystemExit(f'{qualified}: 레시피의 필드가 클래스에 없다: {missing}')

    # 메서드 — 파라미터 이름은 선언에서 온다(레시피의 params 와 같은지는 동등성 증명이 본다).
    done = set()
    for method in (m for m in members if getKind(m) == K_METHOD):
        name = text_of(getSpelling(method))
        if name in listed_methods:
            attributes = ['reflgen::reflect'] + [METHOD_FLAGS[f] for f in listed_methods[name][1]]
            edits.append(Edit(where(rangeStart(getExtent(method)))[2], f'[[{", ".join(attributes)}]] '.encode()))
            if access(method) != 1:
                needs_friend = True
            done.add(name)
    undone = [n for n in listed_methods if n not in done]
    if undone:
        raise SystemExit(f'{qualified}: 레시피의 메서드가 클래스에 없다: {undone}')

    # 비공개 멤버를 반영하면 생성된 서술이 reflgen::access 로 연다.
    record_end = where(rangeEnd(getExtent(record)))[2]
    if needs_friend and not re.search(r'friend\s+(struct\s+)?(::)?reflgen::access', span(name_offset, record_end)):
        brace = raw.index(b'{', name_offset)
        next_line = raw.find(b'\n', brace) + 1
        following = raw[next_line:raw.find(b'\n', next_line)]
        indent = re.match(rb'[ \t]*', following).group(0) or b'    '
        if following.strip().endswith(b':'):  # 접근 지정자 줄이면 그 다음 줄의 들여쓰기
            later = raw[raw.find(b'\n', next_line) + 1:]
            indent = re.match(rb'[ \t]*', later).group(0) or indent
        edits.append(Edit(next_line, indent + b'friend struct reflgen::access;' + newline))

    # 레시피 안이나 바로 위의 주석은 필드를 뺀 이유 같은 설명이다 — 레시피와 함께 사라지므로 사람이 옮긴다.
    comment_above = raw.rfind(b'\n', 0, line_begin - 1) + 1
    has_comments = b'//' in raw[recipe_begin:recipe_end] or raw[comment_above:line_begin].strip().startswith(b'//')
    return {'type': qualified, 'file': file, 'edits': edits, 'entries': entries, 'comments': has_comments}


# ── 쓰기 ────────────────────────────────────────────────────────────────────

def apply_edits(path, edits):
    raw = pathlib.Path(path).read_bytes()
    for edit in sorted(edits, key=lambda e: e.offset, reverse=True):
        raw = raw[:edit.offset] + edit.text + raw[edit.offset + edit.remove:]
    pathlib.Path(path).write_bytes(raw)


def owner_of(header, owners):
    return owners.get(str(pathlib.Path(header).resolve()).lower())


def project_owners():
    owners = {}
    for project in ROOT.rglob('*.vcxproj'):
        if any(p in ('ThirdParty', 'Build', 'Bin', 'vcpkg_installed') for p in project.parts):
            continue
        for include in re.findall(r'<ClInclude Include="([^"]+)"', project.read_text(encoding='utf-8-sig')):
            owners[str((project.parent / include.replace('\\', '/')).resolve()).lower()] = project.stem
    return owners


PARITY_BLOCK = re.compile(r'    static_assert\(parity::matches<([^>]+)>\(\n.*?\)\);\n', re.S)


def parity_file(module, plans, existing=''):
    """이번에 옮긴 타입의 줄을 쓰고, 이미 있던 표(existing)에서 다른 타입의 줄은 그 자리에 둔다."""
    lines = [
        '// reflgen 전환 동등성 증명 — Tools/migration/reflgen_codemod.py 가 옮기기 전의 레시피에서 썼다.',
        '//',
        '// 옮긴 타입마다 다리(ReflgenBridge.h)가 만든 엔진 스키마가 옛 레시피와 필드 이름·순서·속성 타입·속성 값·',
        '// 메서드·파라미터 이름까지 같은지 컴파일 때 단정한다. 엔진 소비자는 스키마 타입에 대한 template 이라 같으면',
        '// 동작도 같다. 이 표는 옛 레시피의 기록이다 — 필드를 더하거나 빼면 여기 줄도 고친다.',
    ]
    here = (ROOT / PARITY_FILES[module]).parent
    lines.append('#include "' + pathlib.Path(os.path.relpath(pathlib.Path(__file__).with_name('ReflgenParity.h'), here)).as_posix() + '"')
    includes = {i for i in re.findall(r'#include "([^"]+)"', existing) if not i.endswith('ReflgenParity.h')}
    for header in {p['file'] for p in plans}:
        relative = pathlib.Path(header).resolve().relative_to(here.resolve()) if pathlib.Path(header).resolve().is_relative_to(here.resolve()) else None
        includes.add((relative or pathlib.Path(header).name).as_posix())
    lines += [f'#include "{include}"' for include in sorted(includes)]
    lines += ['', 'namespace', '{']
    blocks = {}
    for plan in plans:
        items = []
        for kind, name, extra in plan['entries']:
            if kind == 'field':
                attributes = ''.join(f', meta::{a}({arg})' for a, arg in extra)
                items.append(f'parity::field("{name}"{attributes})')
            else:
                # 메서드는 멤버 포인터로 적는다 — method_info 타입(함수·파라미터 수)이 같은지 본다(ReflgenParity.h).
                parameters, flags = extra
                items.append(f'parity::method<&{plan["type"]}::{name}>(' + ', '.join(parameters) + ')'
                             + ''.join(f'.{flag}()' for flag in flags))
        body = ',\n        '.join(items)
        blocks[plan['type']] = f'    static_assert(parity::matches<{plan["type"]}>(\n        {body}));\n'
    kept = []
    for match in PARITY_BLOCK.finditer(existing):
        kept.append(blocks.pop(match.group(1), match.group(0)))
    lines.append(''.join(kept + list(blocks.values())).rstrip('\n'))
    lines += ['}', '']
    return '\n'.join(lines)


def main():
    options = argparse.ArgumentParser()
    options.add_argument('--apply', action='store_true')
    arguments = options.parse_args()

    headers = [p for p in ROOT.rglob('*.h')
               if not any(x in ('ThirdParty', 'Build', 'Bin', 'vcpkg_installed', 'ports') for x in p.parts)
               and re.search(r'static\s+consteval\s+auto\s+reflect\s*\(', p.read_text(encoding='utf-8', errors='replace'))]
    tu = parse_engine(headers)
    plans = collect(tu, headers)
    owners = project_owners()

    by_module = {}
    for plan in plans:
        module = owner_of(plan['file'], owners)
        if module not in PARITY_FILES:
            raise SystemExit(f'{plan["type"]}: header 를 가진 모듈이 없다({plan["file"]}) — ClInclude 에 넣는다')
        by_module.setdefault(module, []).append(plan)

    ignored = sum(e.text.count(b'reflgen::ignore') for p in plans for e in p['edits'])
    print(f'types: {len(plans)} ({", ".join(f"{m} {len(v)}" for m, v in sorted(by_module.items()))})')
    print(f'ignore annotations: {ignored}, friend added: {sum(1 for p in plans for e in p["edits"] if b"friend" in e.text)}')
    commented = [f'{p["type"]} ({pathlib.Path(p["file"]).relative_to(ROOT).as_posix()})' for p in plans if p['comments']]
    if commented:
        print('레시피의 주석을 멤버 옆으로 옮길 것:', ', '.join(commented))
    if not arguments.apply:
        print('dry run — --apply 로 쓴다')
        return

    for file in sorted({p['file'] for p in plans}):
        apply_edits(file, [e for p in plans if p['file'] == file for e in p['edits']])
    for module, module_plans in by_module.items():
        target = ROOT / PARITY_FILES[module]
        existing = target.read_bytes().decode('utf-8').replace('\r\n', '\n') if target.exists() else ''
        target.write_bytes(parity_file(module, module_plans, existing).replace('\n', '\r\n').encode('utf-8'))
        print('wrote', target.relative_to(ROOT))


main()
