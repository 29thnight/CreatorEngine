"""Bound mutation-test payload storage; retain verdicts, manifests and source evidence."""
import json
import os
from pathlib import Path
import shutil


def require_capacity(directory, minimum_gib=30):
    path = Path(directory).resolve()
    while not path.exists():
        path = path.parent
    free = shutil.disk_usage(path).free
    if free < minimum_gib * 2**30:
        raise ValueError(f'Capture refused: {free / 2**30:.2f} GiB free; require {minimum_gib} GiB')


def prune_verified_mutation(directory, source):
    directory, source = Path(directory), Path(source).resolve()
    root = directory.resolve()
    if root == source or root.is_relative_to(source) or source.is_relative_to(root):
        raise ValueError('Mutation cleanup must be separate from source evidence')
    for parent in (directory, *directory.parents):
        if parent.is_symlink() or (hasattr(os.path, 'isjunction') and os.path.isjunction(parent)):
            raise ValueError('Reparse mutation parent')
    files = list(directory.rglob('*.f32'))
    for path in files:
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise ValueError('Mutation pixel outside owned directory')
        for parent in path.parents:
            if parent == directory:
                break
            if hasattr(os.path, 'isjunction') and os.path.isjunction(parent):
                raise ValueError('Reparse mutation child')
    ledger = {'schema': 'artifact.mutation-retention.v1', 'state': 'planned',
              'source': str(source), 'rawPixelReplayAvailable': False,
              'files': [{'path': str(p.relative_to(directory)), 'bytes': p.stat().st_size} for p in files]}
    report = directory / 'pixel-retention.json'
    report.write_text(json.dumps(ledger, indent=2), encoding='utf-8')
    for path in files:
        path.unlink()
    ledger['state'] = 'completed'
    report.write_text(json.dumps(ledger, indent=2), encoding='utf-8')
