"""Fail-closed source/freeze gate for the deliberately small ND-a contract grammar.

This is not a general C++ parser. Contract records permit only fixed-width scalar
or reviewed value fields. Alias, container, pointer, array and new declaration
syntax requires an explicit contract revision, never a silent parser fallback.
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys
import subprocess

HEADERS = (
    'NetWireIds.h', 'INetworkTransport.h', 'NetProtocolHeader.h',
    'INetChannelPolicy.h', 'INetTelemetrySink.h', 'INetworkEntityRegistry.h',
)
ROOT_PATH = pathlib.Path('Engine/Network/Contracts')
STD_HEADERS = {'cstdint', 'cstddef', 'span'}
SCALARS = {f'std::uint{bits}_t' for bits in (8, 16, 32, 64)}
WIRE_RECORDS = {'NetworkObjectId', 'NetPeerId', 'NetSessionId', 'NetComponentTypeId',
                'NetMessageTypeId', 'NetFieldId', 'NetTick', 'NetProtocolHeader'}


class BoundaryError(Exception):
    def __init__(self, code, message):
        super().__init__(f'{code}: {message}')
        self.code = code


def require(value, code, message):
    if not value:
        raise BoundaryError(code, message)


def digest(path):
    # Git and Windows checkouts differ only in line endings.
    data = path.read_bytes().replace(b'\r\n', b'\n')
    return hashlib.sha256(data).hexdigest()


def code_only(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def body_at(text, opening):
    depth = 1
    for index in range(opening + 1, len(text)):
        depth += (text[index] == '{') - (text[index] == '}')
        if depth == 0:
            return text[opening + 1:index]
    raise BoundaryError('NET_WIRE_SYNTAX', 'unclosed contract declaration')


def parse_fields(body, record):
    fields = []
    for statement in body.split(';'):
        statement = statement.strip()
        if not statement:
            continue
        # A record has no member functions, bases, anonymous unions, nested
        # declarations or preprocessor-controlled fields. Initializers are values.
        match = re.fullmatch(r'([\w:]+)\s+(.+)', statement, re.S)
        require(match, 'NET_WIRE_FORBIDDEN_FIELD', f'{record}: unsupported declaration {statement!r}')
        field_type, declarations = match.groups()
        for declaration in declarations.split(','):
            match = re.fullmatch(r'\s*(\w+)\s*(?:\{\s*[\w:]*\s*\}|=\s*[\w:]+)?\s*', declaration)
            require(match, 'NET_WIRE_FORBIDDEN_FIELD', f'{record}: pointer/container/array/member syntax {declaration!r}')
            fields.append([field_type, match[1]])
    return fields


def check_registry(registry):
    require(registry['schemaVersion'] == 1, 'NET_WIRE_IDS', 'unknown registry schema')
    active = {domain: {} for domain in ('message', 'component', 'field')}
    retired = {domain: set() for domain in active}
    seen = {domain: set() for domain in active}
    last_version = 0
    for entry in registry['history']:
        domain, number, name, version = (entry[k] for k in ('domain', 'id', 'name', 'contractVersion'))
        require(domain in active and isinstance(number, int) and not isinstance(number, bool), 'NET_WIRE_IDS', 'invalid ID domain/value')
        require(0 < number <= (0xffffffff if domain == 'component' else 0xffff), 'NET_WIRE_IDS', 'ID outside fixed-width domain')
        require(isinstance(version, int) and version >= last_version, 'NET_WIRE_IDS', 'history must be ordered and append-only')
        require(isinstance(name, str) and bool(re.fullmatch(r'[A-Za-z][A-Za-z0-9_.]*', name)), 'NET_WIRE_IDS', 'invalid stable name')
        last_version = version
        if entry['action'] == 'assign':
            require(number not in seen[domain], 'NET_WIRE_ID_REUSE', f'{domain}:{number} reused, renamed or resurrected')
            require(name not in active[domain].values(), 'NET_WIRE_IDS', 'duplicate active name')
            seen[domain].add(number)
            active[domain][number] = name
        else:
            require(entry['action'] == 'retire' and active[domain].get(number) == name, 'NET_WIRE_IDS', 'retire must match the original assignment')
            del active[domain][number]
            retired[domain].add(number)
    require(registry['active'] == {domain: {str(k): v for k, v in values.items()} for domain, values in active.items()}, 'NET_WIRE_IDS', 'active table differs from history')
    require(registry['tombstones'] == {domain: sorted(values) for domain, values in retired.items()}, 'NET_WIRE_IDS', 'tombstone table differs from history')
    return len(registry['history'])


def check_evolution(current, registry, previous, previous_registry):
    if previous is None:
        require(current['contractVersion'] == 1, 'NET_WIRE_CHANGE_POLICY', 'initial freeze must be version 1')
        return
    require(current['contractVersion'] >= previous['contractVersion'], 'NET_WIRE_CHANGE_POLICY', 'contract version regressed')
    require(current['protocolVersion'] >= previous['protocolVersion'], 'NET_WIRE_CHANGE_POLICY', 'protocol version regressed')
    history = previous_registry['history']
    require(registry['history'][:len(history)] == history, 'NET_WIRE_CHANGE_POLICY', 'ID history was deleted or rewritten')
    changes = previous['changes']
    require(current['changes'][:len(changes)] == changes, 'NET_WIRE_CHANGE_POLICY', 'contract change history was rewritten')
    changed = any(current[key] != previous[key] for key in ('headers', 'registrySha256', 'records', 'enums', 'classes'))
    require(not changed or current['contractVersion'] > previous['contractVersion'],
            'NET_WIRE_CHANGE_POLICY', 'changed contract must increment contractVersion')
    wire_changed = any(current['records'].get(name) != record for name, record in previous['records'].items() if record['wire'])
    wire_changed |= any(record['wire'] and name not in previous['records'] for name, record in current['records'].items())
    # Existing static ID retirement/reassignment changes compatibility. Additions
    # still require the distribution compatibility hash and allowlist to agree.
    for domain, assignments in previous_registry['active'].items():
        wire_changed |= any(registry['active'][domain].get(number) != name for number, name in assignments.items())
    require(not wire_changed or current['protocolVersion'] > previous['protocolVersion'],
            'NET_WIRE_CHANGE_POLICY', 'wire field or existing ID change must increment protocolVersion')


def check_base(root, base_ref, current, registry):
    resolved = subprocess.run(['git', '-C', str(root), 'rev-parse', '--verify', '--end-of-options', f'{base_ref}^{{commit}}'], capture_output=True, text=True)
    require(resolved.returncode == 0 and bool(re.fullmatch(r'[0-9a-f]{40,64}', resolved.stdout.strip())),
            'NET_WIRE_CHANGE_POLICY', 'base ref is not a readable commit')
    base_ref = resolved.stdout.strip()
    path = ROOT_PATH.as_posix()
    result = subprocess.run(['git', '-C', str(root), 'ls-tree', base_ref, '--', f'{path}/net-contracts.lock.json'], capture_output=True, text=True)
    require(result.returncode == 0, 'NET_WIRE_CHANGE_POLICY', 'base ref cannot be resolved')
    previous = previous_registry = None
    if result.stdout.strip():
        def read(name):
            content = subprocess.run(['git', '-C', str(root), 'show', f'{base_ref}:{path}/{name}'], capture_output=True, text=True, encoding='utf-8')
            require(content.returncode == 0, 'NET_WIRE_CHANGE_POLICY', f'cannot read baseline {name}')
            return json.loads(content.stdout)
        previous = read('net-contracts.lock.json')
        previous_registry = read('NetWireIds.registry.json')
    check_evolution(current, registry, previous, previous_registry)


def check(root):
    folder = root / ROOT_PATH
    manifest = json.loads((folder / 'net-contracts.lock.json').read_text(encoding='utf-8-sig'))
    require(manifest['schemaVersion'] == 1 and manifest['contractVersion'] >= 1, 'NET_WIRE_FREEZE', 'unknown contract manifest schema')
    require({name for name, record in manifest['records'].items() if record['wire']} == WIRE_RECORDS,
            'NET_WIRE_DECLARATION', 'wire/local classification cannot be relabeled to bypass the boundary')
    for source in (root / 'Engine/Network').rglob('*'):
        if source.is_file() and source.suffix.lower() in ('.h', '.hpp', '.hxx', '.inl', '.ixx'):
            relative = source.relative_to(root / 'Engine/Network')
            require(relative.parts[0] in ('Contracts', 'Adapters'), 'NET_WIRE_INCLUDE',
                    f'unregistered public network header {relative}; extend the reviewed catalog first')
    actual = {p.name for p in folder.rglob('*') if p.is_file() and p.suffix.lower() in ('.h', '.hpp', '.hxx', '.inl', '.ixx', '.cpp')}
    require(actual == set(HEADERS), 'NET_WIRE_INCLUDE', f'contract source inventory differs: {actual ^ set(HEADERS)}')
    require(set(manifest['headers']) == set(HEADERS), 'NET_WIRE_FREEZE', 'six contract headers must remain registered')
    records, enums, classes, versions, constants = {}, {}, set(), set(), {}
    for name in HEADERS:
        text = code_only((folder / name).read_text(encoding='utf-8-sig'))
        for directive in re.findall(r'^\s*#\s*([^\n]+)', text, flags=re.M):
            if directive.strip() == 'pragma once':
                continue
            include = re.fullmatch(r'include\s*([<"])([^>"]+)[>"]\s*', directive)
            require(include is not None, 'NET_WIRE_INCLUDE', f'{name}: macros/conditional declarations are not allowed')
            require((include[1] == '<' and include[2] in STD_HEADERS)
                    or (include[1] == '"' and include[2] in HEADERS), 'NET_WIRE_INCLUDE', f'{name}: unreviewed dependency {include[2]}')
        text = re.sub(r'^\s*#.*$', '', text, flags=re.M)
        require(not re.search(r'\b(using|typedef)\b', text), 'NET_WIRE_ALIAS', f'{name}: aliases can hide forbidden wire fields')
        require(not re.search(r'\b(size_t|ptrdiff_t|intptr_t|uintptr_t|EntityHandle|ScriptObjectHandle|ObjectHandle|HashedGuid|HashingString|NodeRef)\b|\b(YAML|ryml|c4|nlohmann)\s*::|std::(vector|basic_string|string|map|unordered_map|function|shared_ptr|unique_ptr|optional|variant)\b', text), 'NET_WIRE_FORBIDDEN_TYPE', f'{name}: engine/platform/container/parser type leaked')
        for match in re.finditer(r'\bstruct\s+(\w+)\s*(?:final\s*)?\{', text):
            require(match[1] not in records, 'NET_WIRE_SYNTAX', f'duplicate record {match[1]}')
            records[match[1]] = {'header': name, 'fields': parse_fields(body_at(text, match.end() - 1), match[1])}
        require(len(re.findall(r'\bstruct\b', text)) == len(re.findall(r'\bstruct\s+\w+\s*(?:final\s*)?\{', text)),
                'NET_WIRE_SYNTAX', f'{name}: bases/anonymous/forward/attributed records need a reviewed grammar change')
        for match in re.finditer(r'\benum\s+class\s+(\w+)\s*:\s*([\w:]+)\s*\{', text):
            require(match[2] in SCALARS, 'NET_WIRE_FORBIDDEN_TYPE', f'{match[1]}: enum must use a fixed-width integer')
            enums[match[1]] = match[2]
        without_enums = re.sub(r'\benum\s+class\b', 'enum_class', text)
        classes.update(re.findall(r'\bclass\s+(\w+)\s*\{', without_enums))
        for key in ('contract_version', 'protocol_version'):
            match = re.search(rf'\b{key}\s*=\s*(\d+)\s*;', text)
            if match:
                versions.add(key)
                expected = manifest['contractVersion' if key == 'contract_version' else 'protocolVersion']
                require(int(match[1]) == expected, 'NET_WIRE_FREEZE', f'{key} differs from manifest')
        for match in re.finditer(r'inline\s+constexpr\s+(NetMessageTypeId|NetComponentTypeId|NetFieldId)\s+(\w+)\s*\{(\d+)\}\s*;', text):
            constants[match[2]] = int(match[3])
    require(versions == {'contract_version', 'protocol_version'}, 'NET_WIRE_FREEZE', 'required version declarations missing')
    require(set(records) == set(manifest['records']), 'NET_WIRE_DECLARATION', 'added, removed or unparsed contract record')
    require(enums == manifest['enums'] and classes == set(manifest['classes']), 'NET_WIRE_DECLARATION', 'enum/class inventory differs from frozen contract')
    for name, record in records.items():
        frozen = manifest['records'][name]
        require(record['header'] == frozen['header'], 'NET_WIRE_DECLARATION', f'{name}: record moved')
        for field_type, field_name in record['fields']:
            allowed = field_type in SCALARS or field_type in enums or field_type in records
            if frozen['wire'] and field_type in records:
                allowed = manifest['records'][field_type]['wire']
            require(allowed, 'NET_WIRE_FORBIDDEN_FIELD', f'{name}.{field_name}: forbidden type {field_type}')
        require(record['fields'] == frozen['fields'], 'NET_WIRE_FIELD_SCHEMA', f'{name}: field order/type/name differs from frozen schema')
    registry_path = folder / 'NetWireIds.registry.json'
    registry = json.loads(registry_path.read_text(encoding='utf-8-sig'))
    events = check_registry(registry)
    require(set(constants) == set(manifest['idConstants']), 'NET_WIRE_IDS', 'static ID constant inventory differs')
    for name, rule in manifest['idConstants'].items():
        require(constants[name] == rule['id'] and registry['active'][rule['domain']].get(str(rule['id'])) == rule['name'],
                'NET_WIRE_IDS', f'{name}: source constant differs from ID registry')
    require(all(event['contractVersion'] <= manifest['contractVersion'] for event in registry['history']),
            'NET_WIRE_CHANGE_POLICY', 'ID event belongs to an undeclared future contract')
    require(digest(registry_path) == manifest['registrySha256'], 'NET_WIRE_FREEZE', 'registry changed without a reviewed freeze revision')
    for name in HEADERS:
        require(digest(folder / name) == manifest['headers'][name], 'NET_WIRE_FREEZE', f'{name}: frozen source changed; follow the contract change procedure')
    require([event['version'] for event in manifest['changes']] == list(range(1, manifest['contractVersion'] + 1)),
            'NET_WIRE_FREEZE', 'contract changes must be contiguous and append-only')
    return {'result': 'NET_WIRE_BOUNDARY_OK', 'headers': len(HEADERS), 'records': len(records),
            'wireRecords': sum(r['wire'] for r in manifest['records'].values()), 'registryEvents': events,
            'contractVersion': manifest['contractVersion'], 'protocolVersion': manifest['protocolVersion']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--base-ref')
    args = parser.parse_args()
    try:
        result = check(args.root.resolve())
        if args.base_ref:
            folder = args.root / ROOT_PATH
            check_base(args.root, args.base_ref, json.loads((folder/'net-contracts.lock.json').read_text(encoding='utf-8-sig')),
                       json.loads((folder/'NetWireIds.registry.json').read_text(encoding='utf-8-sig')))
            result['changePolicyBase'] = args.base_ref
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result))
    except (BoundaryError, OSError, KeyError, TypeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
