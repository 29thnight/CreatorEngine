"""Exercise the shipped preview through the real Debug editor, including corruption fallback.
Existing user caches and the package are preserved. Requires a built preview package.
"""
import argparse, hashlib, json, os, subprocess
from pathlib import Path


def main():
    args = argparse.ArgumentParser()
    args.add_argument('--output', required=True)
    args.add_argument('--configuration', default='Debug', choices=['Debug', 'Release'])
    opts = args.parse_args()
    root = Path(__file__).resolve().parents[2]
    run = Path(opts.output).resolve()
    run.mkdir(exist_ok=False, parents=True)
    exe = root / f'Bin/x64-{opts.configuration}/Editor/CreatorEditor.exe'
    package = exe.parent / 'EditorResources/Preview/a5033a2b-a423-8a02-9245-8b968e8e707b.slang.scene-cache'
    accepted = package.read_bytes()
    results = []
    try:
        for case in ['accepted', 'corrupt-product']:
            folder = run / case
            folder.mkdir()
            if case == 'corrupt-product':
                damaged = bytearray(accepted)
                damaged[-1] ^= 1  # Cooked-program checksum, after valid dependency/header checks.
                package.write_bytes(damaged)
            script = folder / 'commands.txt'
            script.write_text(f'model.async "{root}/Dynamic_CPP/Assets/Models/CreatorRobot.glb"\nmodel.async wait\nwait 300\nquit\n', encoding='utf-8')
            env = os.environ.copy()
            env.update(LOCALAPPDATA=str(run/'cache'), CE_MATERIAL_CACHE_ROOT=str(run/'cache/MaterialPrograms'),
                       CE_TRACE_SHADER_COMPILE='1', CE_EDITOR_MODEL_PREVIEW='1')
            with (folder/'stdout.txt').open('wb') as stdout, (folder/'stderr.txt').open('wb') as stderr:
                process = subprocess.run([str(exe), '--development-project', str(root/'Dynamic_CPP'), '--smoke-offscreen',
                    '--commandlet-script', str(script), '--result-file', str(folder/'results.jsonl')],
                    cwd=root, env=env, stdout=stdout, stderr=stderr, timeout=180)
            rows = [json.loads(line) for line in (folder/'results.jsonl').read_text(encoding='utf-8-sig').splitlines()]
            assert process.returncode == 0 and rows and all(row['status']=='succeeded' for row in rows), (case, rows)
            text = (folder/'stdout.txt').read_text(encoding='utf-8', errors='replace')
            shader = (folder/'stderr.txt').read_text(encoding='utf-8', errors='replace')
            preview_requests = [line for line in shader.splitlines() if 'a5033a2b-a423-8a02-9245-8b968e8e707b' in line]
            if case == 'accepted':
                assert '[preview.package] hit stages=11' in text and not preview_requests, text
            else:
                assert 'miss reason=cooked-program' in text and '[preview.package] hit' not in text, text
                assert len(preview_requests)==11 and all('compiled=1 failed=0' in line for line in preview_requests), preview_requests
                assert all('Lookup' not in line and 'Shadow' not in line for line in preview_requests)
            assert 'phase=preview' in text and 'phase=original' in text, text
            results.append(dict(case=case, passed=True, previewShaderRequests=len(preview_requests)))
    finally:
        package.write_bytes(accepted)
    assert package.read_bytes()==accepted
    summary = dict(passed=True, cases=results, packageRestored=True,
        packageSha256=hashlib.sha256(accepted).hexdigest(),
        runtimeSha256=hashlib.sha256(exe.with_name('CreatorEditor.runtime.dll').read_bytes()).hexdigest())
    (run/'summary.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
