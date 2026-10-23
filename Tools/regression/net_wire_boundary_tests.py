import argparse
import copy
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
from net_wire_boundary import check_registry, check_evolution, BoundaryError
from network_handoff_suite import validate

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
folder = pathlib.Path('Engine/Network/Contracts')
gate = root / 'Tools/regression/net_wire_boundary.py'
results = []
mutants = [
    ('size_t', 'std::size_t value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('raw_pointer', 'void* value{};', 'NET_WIRE_FORBIDDEN_FIELD'),
    ('reference', 'std::uint64_t& value;', 'NET_WIRE_FORBIDDEN_FIELD'),
    ('EntityHandle', 'EntityHandle value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('HashedGuid', 'HashedGuid value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('vector', 'std::vector<std::uint64_t> value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('string', 'std::string value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('yaml', 'YAML::Node value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('json', 'nlohmann::json value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('ryml', 'ryml::NodeRef value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('pointer_integer', 'std::uintptr_t value{};', 'NET_WIRE_FORBIDDEN_TYPE'),
    ('platform_integer', 'unsigned long value{};', 'NET_WIRE_FORBIDDEN_FIELD'),
    ('fixed_array', 'std::uint64_t value[2]{};', 'NET_WIRE_FORBIDDEN_FIELD'),
    ('local_token', 'NetEntityToken value{};', 'NET_WIRE_FORBIDDEN_FIELD'),
]
with tempfile.TemporaryDirectory(prefix='nda-mutations-', dir=args.output.parent) as temp:
    temporary = pathlib.Path(temp)

    def run_case(name, mutate, diagnostic, success=False):
        target = temporary / name
        shutil.copytree(root / folder, target / folder)
        mutate(target / folder)
        process = subprocess.run([sys.executable, str(gate), '--root', str(target)], capture_output=True, text=True)
        output = process.stdout + process.stderr
        if (process.returncode == 0) != success or diagnostic not in output:
            raise RuntimeError(f'{name}: unexpected code={process.returncode} output={output}')
        results.append({'name': name, 'exitCode': process.returncode, 'diagnostic': diagnostic})

    def replace_id(target, declaration):
        path = target / 'NetWireIds.h'
        path.write_text(path.read_text().replace('std::uint64_t value{};', declaration, 1), encoding='utf-8')

    run_case('baseline', lambda _: None, 'NET_WIRE_BOUNDARY_OK', True)
    for name, declaration, diagnostic in mutants:
        run_case(name, lambda target, declaration=declaration: replace_id(target, declaration), diagnostic)
    run_case('alias', lambda target: replace_id(target, 'HiddenCount value{};') or
             (target / 'NetWireIds.h').write_text((target / 'NetWireIds.h').read_text().replace('namespace ce::net', 'using HiddenCount = std::uint64_t;\nnamespace ce::net')), 'NET_WIRE_ALIAS')
    run_case('transitive_include', lambda target: (target / 'INetworkTransport.h').write_text(
        '#include "../../SceneRuntime/Entity.h"\n' + (target / 'INetworkTransport.h').read_text()), 'NET_WIRE_INCLUDE')
    run_case('unregistered_header', lambda target: (target / 'Hidden.h').write_text('struct Hidden { void* ptr; };'), 'NET_WIRE_INCLUDE')
    def outside_header(target):
        path = target.parent / 'Wire/Hidden.h'
        path.parent.mkdir()
        path.write_text('struct Hidden { void* ptr; };')
    run_case('unregistered_public_wire_header', outside_header, 'NET_WIRE_INCLUDE')
    def relabel_wire(target):
        path = target / 'net-contracts.lock.json'
        data = json.loads(path.read_text()); data['records']['NetworkObjectId']['wire'] = False
        path.write_text(json.dumps(data))
    run_case('wire_relabel', relabel_wire, 'NET_WIRE_DECLARATION')
    run_case('conditional_field', lambda target: (target / 'NetWireIds.h').write_text(
        '#if 0\nstruct Hidden { void* ptr; };\n#endif\n' + (target / 'NetWireIds.h').read_text()), 'NET_WIRE_INCLUDE')
    run_case('unparsed_inheritance', lambda target: (target / 'NetWireIds.h').write_text(
        (target / 'NetWireIds.h').read_text().replace('NetworkObjectId final {', 'NetworkObjectId : HiddenBase {')), 'NET_WIRE_SYNTAX')
    run_case('new_wire_field', lambda target: replace_id(target, 'std::uint64_t value{}, extra{};'), 'NET_WIRE_FIELD_SCHEMA')
    run_case('frozen_source', lambda target: (target / 'NetWireIds.h').write_text(
        (target / 'NetWireIds.h').read_text() + '\n// unreviewed edit\n'), 'NET_WIRE_FREEZE')
    run_case('id_constant_drift', lambda target: (target / 'NetWireIds.h').write_text(
        (target / 'NetWireIds.h').read_text().replace('handshake_message{1}', 'handshake_message{2}')), 'NET_WIRE_IDS')
    run_case('crlf_checkout', lambda target: [(path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))) for path in target.glob('*')], 'NET_WIRE_BOUNDARY_OK', True)
    registry = json.loads((root / folder / 'NetWireIds.registry.json').read_text())
    legitimate = copy.deepcopy(registry)
    legitimate['history'].append({'contractVersion': 2, 'action': 'retire', 'domain': 'message', 'id': 1, 'name': 'Handshake'})
    legitimate['active']['message'] = {}
    legitimate['tombstones']['message'] = [1]
    check_registry(legitimate)
    results.append({'name': 'legitimate_retirement', 'exitCode': 0, 'diagnostic': 'registry replay accepted'})
    reused = copy.deepcopy(legitimate)
    reused['history'].append({'contractVersion': 2, 'action': 'assign', 'domain': 'message', 'id': 1, 'name': 'Other'})
    reused['active']['message'] = {'1': 'Other'}
    try:
        check_registry(reused)
        raise RuntimeError('Retired ID reuse was accepted')
    except BoundaryError as error:
        if error.code != 'NET_WIRE_ID_REUSE':
            raise
        results.append({'name': 'retired_id_reuse', 'exitCode': 1, 'diagnostic': error.code})
    baseline = json.loads((root / folder / 'net-contracts.lock.json').read_text())
    revised = copy.deepcopy(baseline)
    revised['contractVersion'] = 2
    revised['changes'].append({'version': 2, 'protocolVersion': 1, 'reason': 'test local-only interface change'})
    revised['headers']['INetworkTransport.h'] = 'changed'
    check_evolution(revised, registry, baseline, registry)
    results.append({'name': 'legitimate_local_contract_revision', 'exitCode': 0, 'diagnostic': 'change policy accepted'})
    cases = []
    no_bump = copy.deepcopy(baseline); no_bump['headers']['INetworkTransport.h'] = 'changed'
    cases.append(('contract_change_without_version', no_bump, registry))
    wire_change = copy.deepcopy(revised); wire_change['records']['NetworkObjectId']['fields'] = [['std::uint32_t', 'value']]
    cases.append(('wire_change_without_protocol', wire_change, registry))
    rewritten = copy.deepcopy(registry); rewritten['history'][0]['name'] = 'Renamed'
    cases.append(('history_rewrite', revised, rewritten))
    rewritten_changes = copy.deepcopy(revised); rewritten_changes['changes'][0]['reason'] = 'rewritten'
    cases.append(('change_log_rewrite', rewritten_changes, registry))
    cases.append(('retire_without_protocol', revised, legitimate))
    for name, changed_manifest, changed_registry in cases:
        try:
            check_evolution(changed_manifest, changed_registry, baseline, registry)
            raise RuntimeError(f'{name}: invalid contract evolution accepted')
        except BoundaryError as error:
            if error.code != 'NET_WIRE_CHANGE_POLICY':
                raise
            results.append({'name': name, 'exitCode': 1, 'diagnostic': error.code})
    compatible_revision = copy.deepcopy(wire_change); compatible_revision['protocolVersion'] = 2
    check_evolution(compatible_revision, registry, baseline, registry)
    results.append({'name': 'legitimate_wire_protocol_revision', 'exitCode': 0, 'diagnostic': 'change policy accepted'})
    suite = json.loads((root / 'Tools/regression/network-handoff.suite.json').read_text())
    validate(root, suite)
    for name, mutate in (
        ('remove_gate', lambda manifest: manifest['gates'].pop(0)),
        ('redirect_gate', lambda manifest: manifest['gates'][0].update(script='Tools/regression/verify-simulation-exchange.ps1')),
        ('skip_configuration', lambda manifest: manifest['gates'][0].update(configuration='none')),
    ):
        changed = copy.deepcopy(suite); mutate(changed)
        try:
            validate(root, changed)
            raise RuntimeError(f'{name}: mandatory gate bypass accepted')
        except ValueError as error:
            if 'NET_SUITE_REGISTRATION' not in str(error):
                raise
            results.append({'name': name, 'exitCode': 1, 'diagnostic': 'NET_SUITE_REGISTRATION'})
args.output.write_text(json.dumps({'result': 'NET_WIRE_CANARIES_OK', 'cases': results}, indent=2) + '\n', encoding='utf-8')
print(f'NET_WIRE_CANARIES_OK cases={len(results)} temporaryCopies=removed productionSources=unchanged')
