# 리플렉션 속성 배치 규약(docs/design/CodingConventions.md §7.4)을 적용하거나 검사한다.
#
#   변수·메서드에 다는 [[reflgen::…]]·[[creator::…]] 는 자기 줄에 두고, 선언은 그 아래 줄에 둔다.
#   속성을 단 선언(바로 위의 설명 주석을 포함한 한 덩어리)이 앞뒤 내용과 이어져 있으면 빈 줄로 가른다.
#   범위의 경계 — { 로 끝나는 줄, } 로 시작하는 줄, 접근 지정자 — 와는 가르지 않는다.
#   클래스 머리의 속성(class [[reflgen::reflect]] X)은 대상이 아니다.
#
#   python Tools/migration/reflgen_attribute_layout.py --check       # 어긋난 파일을 적고 1 로 끝난다
#   python Tools/migration/reflgen_attribute_layout.py --apply       # 고쳐 쓴다(두 번 돌려도 같다)
#   python Tools/migration/reflgen_attribute_layout.py --self-test   # 합성 표본으로 변환 규칙을 확인한다
#
# 바이트 단위로 다룬다 — 헤더마다 인코딩(CP949·UTF-8 BOM)과 줄 끝(CRLF·LF)이 다르다. reflgen_codemod.py 가
# 속성을 단 뒤 이 변환을 거친다.
import argparse
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SEARCH = ('Engine', 'Editor', 'Player')
SKIP_DIRS = {'vcpkg_installed', 'ThirdParty', 'Build', 'Bin', '.git', 'packages', 'x64'}
ATTRIBUTE_START = re.compile(rb'^([ \t]*)\[\[(?:reflgen|creator)::')
ANY_ATTRIBUTE_LINE = re.compile(rb'(?m)^[ \t]*\[\[(?:reflgen|creator)::')
ACCESS_LABEL = re.compile(rb'^[ \t]*(public|private|protected)[ \t]*:')


def attribute_end(line, start):
    """line[start:] 이 [[ 로 시작할 때 짝이 맞는 ]] 다음 위치. 따옴표·괄호 안의 ] 는 넘긴다."""
    i = start + 2
    depth = 0
    quote = None
    while i < len(line):
        c = line[i:i + 1]
        if quote:
            if c == b'\\':
                i += 2
                continue
            if c == quote:
                quote = None
        elif c in (b'"', b"'"):
            quote = c
        elif c == b'(':
            depth += 1
        elif c == b')':
            depth -= 1
        elif depth == 0 and line[i:i + 2] == b']]':
            return i + 2
        i += 1
    return -1


def code_part(text):
    """줄 끝 // 주석을 뗀 코드(문자열 안의 // 는 둔다)."""
    quote = None
    i = 0
    while i < len(text):
        c = text[i:i + 1]
        if quote:
            if c == b'\\':
                i += 2
                continue
            if c == quote:
                quote = None
        elif c in (b'"', b"'"):
            quote = c
        elif text[i:i + 2] == b'//':
            return text[:i]
        i += 1
    return text


def depth_change(text):
    code = code_part(text)
    code = re.sub(rb'"(\\.|[^"\\])*"', b'""', code)
    code = re.sub(rb"'(\\.|[^'\\])*'", b"''", code)
    return sum(code.count(o) for o in (b'(', b'{', b'[')) - sum(code.count(c) for c in (b')', b'}', b']'))


def is_blank(text):
    return text.strip() == b''


def is_comment(text):
    stripped = text.lstrip()
    return stripped.startswith((b'//', b'/*', b'*'))


def opens_scope(text):
    """이 줄 뒤에 오는 덩어리와 빈 줄로 가르지 않는다 — 범위를 여는 줄, 접근 지정자."""
    return code_part(text).rstrip().endswith(b'{') or bool(ACCESS_LABEL.match(text))


def closes_scope(text):
    """이 줄 앞의 덩어리와 빈 줄로 가르지 않는다 — 범위를 닫는 줄, 접근 지정자."""
    return text.lstrip().startswith(b'}') or bool(ACCESS_LABEL.match(text))


def layout(data):
    """(바뀐 바이트, 속성 덩어리 수)."""
    eol = b'\r\n' if b'\r\n' in data else b'\n'
    lines = data.split(eol)
    trailing = lines[-1] == b''
    if trailing:
        lines = lines[:-1]

    # 1) 속성을 자기 줄로 떼고, 덩어리(주석 + 속성 + 선언)마다 번호를 매긴다.
    marked = []  # [줄, 덩어리 번호 또는 None]
    blocks = 0
    i = 0
    while i < len(lines):
        line = lines[i]
        match = ATTRIBUTE_START.match(line)
        if not match:
            marked.append([line, None])
            i += 1
            continue
        indent = match.group(1)
        start = len(indent)
        end = attribute_end(line, start)
        if end < 0:
            raise ValueError(f'닫히지 않은 속성: {line!r}')
        rest = line[end:].lstrip()
        while rest.startswith(b'[['):  # 한 줄에 이어 쓴 속성 묶음([[a]] [[b]])은 한 줄에 둔다
            more = attribute_end(rest, 0)
            if more < 0:
                raise ValueError(f'닫히지 않은 속성: {line!r}')
            rest = rest[more:].lstrip()
        attribute = line[start:len(line) - len(rest)].rstrip()

        blocks += 1
        first = len(marked)  # 바로 위의 설명 주석은 이 덩어리에 딸린다
        while first > 0 and marked[first - 1][1] is None and is_comment(marked[first - 1][0]):
            first -= 1
        for entry in marked[first:]:
            entry[1] = blocks
        marked.append([indent + attribute, blocks])

        # 선언: 속성 뒤의 나머지(새 줄이 되므로 끝 공백을 뗀다), 없으면 다음 줄부터. 괄호가 닫히고 ; 또는 } 로
        # 끝나는 줄까지.
        i += 1
        if rest:
            declaration = [indent + rest.rstrip(b' \t')]
        else:
            while i < len(lines) and is_blank(lines[i]):
                i += 1  # 속성과 선언 사이의 빈 줄은 없앤다
            if i >= len(lines):
                raise ValueError(f'속성 뒤에 선언이 없다: {line!r}')
            declaration = [lines[i]]
            i += 1
        depth = sum(depth_change(text) for text in declaration)
        while depth > 0 or not code_part(declaration[-1]).rstrip().endswith((b';', b'}')):
            if i >= len(lines):
                raise ValueError(f'선언이 끝나지 않는다: {declaration[0]!r}')
            declaration.append(lines[i])
            depth += depth_change(lines[i])
            i += 1
        marked.extend([text, blocks] for text in declaration)

    # 2) 덩어리의 앞뒤를 빈 줄로 가른다(범위의 경계와 이미 빈 줄은 그대로).
    result = []
    for index, (text, block) in enumerate(marked):
        previous = marked[index - 1] if index > 0 else None
        if previous is not None and previous[1] != block and not is_blank(text) and not is_blank(previous[0]):
            enters_block = block is not None and not opens_scope(previous[0])
            leaves_block = previous[1] is not None and block is None and not closes_scope(text)
            if enters_block or leaves_block:
                result.append(b'')
        result.append(text)

    body = eol.join(result)
    return (body + eol if trailing else body), blocks


def headers():
    for top in SEARCH:
        for dirpath, dirnames, filenames in os.walk(ROOT / top):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                if name.endswith(('.h', '.hpp')):
                    yield pathlib.Path(dirpath) / name


SAMPLE = b'''class [[reflgen::reflect]] sample : public meta::identity<sample, Component>
{
    friend struct reflgen::access;
public:
    [[reflgen::reflect, creator::read_only_in_inspector]] bool IsOpen() const;
    void Plain();
    // runtime cache
    [[reflgen::ignore]] float m_cache = 0.0f; //trailing \t
    [[reflgen::ignore]] std::array<int, 2>
        m_pair{};
    int m_saved = 1;
    [[reflgen::reflect]]

    int Wide() { return 1; }
};
'''

EXPECTED = b'''class [[reflgen::reflect]] sample : public meta::identity<sample, Component>
{
    friend struct reflgen::access;
public:
    [[reflgen::reflect, creator::read_only_in_inspector]]
    bool IsOpen() const;

    void Plain();

    // runtime cache
    [[reflgen::ignore]]
    float m_cache = 0.0f; //trailing

    [[reflgen::ignore]]
    std::array<int, 2>
        m_pair{};

    int m_saved = 1;

    [[reflgen::reflect]]
    int Wide() { return 1; }
};
'''


def self_test():
    failures = []
    for eol in (b'\n', b'\r\n'):
        sample = SAMPLE.replace(b'\n', eol)
        expected = EXPECTED.replace(b'\n', eol)
        once, blocks = layout(sample)
        if once != expected:
            failures.append(f'변환 결과가 기대와 다르다(eol={eol!r}):\n{once.decode()}')
        if blocks != 4:
            failures.append(f'속성 덩어리가 4개여야 하는데 {blocks}개다')
        if layout(expected)[0] != expected:
            failures.append(f'규약대로인 표본을 바꿨다(eol={eol!r}) — 두 번 돌리면 같아야 한다')
    return failures


def main():
    options = argparse.ArgumentParser()
    mode = options.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--apply', action='store_true')
    mode.add_argument('--self-test', action='store_true')
    arguments = options.parse_args()

    if arguments.self_test:
        failures = self_test()
        for failure in failures:
            print(failure)
        print('self-test:', 'FAIL' if failures else 'ok')
        return 1 if failures else 0

    scanned = 0
    blocks = 0
    offending = []
    for path in headers():
        data = path.read_bytes()
        if not ANY_ATTRIBUTE_LINE.search(data):
            continue
        scanned += 1
        new, count = layout(data)
        blocks += count
        if new != data:
            offending.append(path)
            if arguments.apply:
                path.write_bytes(new)
    print(f'headers with reflection attributes: {scanned}, attribute blocks: {blocks}')
    for path in offending:
        print(('fixed: ' if arguments.apply else 'layout differs: ') + path.relative_to(ROOT).as_posix())
    if arguments.check and offending:
        print('python Tools/migration/reflgen_attribute_layout.py --apply 로 고친다')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
