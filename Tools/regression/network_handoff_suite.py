"""Validate mandatory regression registration before running any child process."""
import json
import pathlib

REQUIRED = {
    'HeadlessBoundary': ('Tools/regression/verify-headless-boundary.ps1', 'none'),
    'ND-a': ('Tools/regression/verify-net-wire-boundary.ps1', 'all'),
    'W4-ND-b': ('Tools/regression/verify-scene-observation.ps1', 'all'),
    'N1': ('Tools/regression/verify-archive-parser-boundary.ps1', 'all'),
    'N3': ('Tools/regression/verify-simulation-exchange.ps1', 'matrix'),
    'PhysicsABI': ('Tools/regression/verify-physics-script-abi.ps1', 'all'),
    'PhysicsIdentity': ('Tools/regression/verify-physics-body-identity.ps1', 'matrix'),
    'LifecycleEnums': ('ScriptCore/check-lifecycle-enums.ps1', 'none'),
}


def validate(root, manifest):
    if manifest.get('schemaVersion') != 1 or manifest.get('suite') != 'NetworkHandoff':
        raise ValueError('NET_SUITE_REGISTRATION: invalid suite schema')
    gates = manifest.get('gates', [])
    if len(gates) != len(REQUIRED) or {gate.get('id') for gate in gates} != set(REQUIRED):
        raise ValueError('NET_SUITE_REGISTRATION: missing, duplicated or unreviewed required gate')
    if gates[0]['id'] != 'ND-a':
        raise ValueError('NET_SUITE_REGISTRATION: ND-a must run before downstream gates')
    for gate in gates:
        if (gate.get('script'), gate.get('configuration')) != REQUIRED[gate['id']]:
            raise ValueError('NET_SUITE_REGISTRATION: mandatory gate path/mode changed')
        path = (root / gate['script']).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            raise ValueError('NET_SUITE_REGISTRATION: missing/outside-workspace gate')
    return gates


if __name__ == '__main__':
    import argparse
    import sys
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=pathlib.Path, required=True)
    parser.add_argument('--manifest', type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        validate(args.root, json.loads(args.manifest.read_text(encoding='utf-8-sig')))
        print(f'NET_SUITE_REGISTRATION_OK mandatory={len(REQUIRED)}')
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
