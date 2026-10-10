[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$AssetRoot = '',
    [int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($AssetRoot)) {
    $AssetRoot = Join-Path $repo 'Dynamic_CPP/Assets'
}
$AssetRoot = [IO.Path]::GetFullPath($AssetRoot)
$work = Join-Path $repo ('Build/TextureRoundtrip-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$script = Join-Path $work 'commands.txt'
$result = Join-Path $work 'results.jsonl'
$rootArgument = $AssetRoot.Replace('\', '/')
@(('assets.decodeab "' + $rootArgument + '" 0'),
    ('assets.decodeabhdr "' + $rootArgument + '"'), 'quit') |
    Set-Content -LiteralPath $script -Encoding utf8
$oldWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
$oldIni = $env:CREATOR_EDITOR_LEGACY_INI
try {
    $env:CREATOR_EDITOR_WORKSPACE_DIR = Join-Path $work 'Workspace'
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $work 'missing.ini'
    $process = Start-Process -FilePath (Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe") `
        -ArgumentList '--commandlet-script', ('"' + $script + '"'), '--result-format', 'jsonl', `
            '--result-file', ('"' + $result + '"') -WorkingDirectory $repo -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $work 'stdout.log') `
        -RedirectStandardError (Join-Path $work 'stderr.log') -PassThru
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "Texture roundtrip timed out: $work"
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Texture roundtrip process failed: $($process.ExitCode), $work"
    }
} finally {
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $oldWorkspace
    $env:CREATOR_EDITOR_LEGACY_INI = $oldIni
}
$records = @(Get-Content -LiteralPath $result | ForEach-Object { $_ | ConvertFrom-Json })
foreach ($command in @('assets.decodeab', 'assets.decodeabhdr')) {
    $rows = @($records | Where-Object command -eq $command)
    if ($rows.Count -ne 1 -or $rows[0].status -ne 'succeeded') {
        throw "Missing or failed result for $command`: $work"
    }
    $data = $rows[0].data
    $validation = if ($command -eq 'assets.decodeab') {
        'directxtex.rgba8_png_roundtrip'
    } else {
        'directxtex.hdr_float_dds_roundtrip'
    }
    if ($data.validation -ne $validation -or $data.selected -le 0 -or
        $data.found -ne $data.selected -or $data.compared -ne $data.selected -or
        $data.identical -ne $data.compared -or $data.failed -ne 0 -or $data.walkFailed -or
        ($command -eq 'assets.decodeab' -and $data.pngByteFixtures -ne 'passed')) {
        throw "Incomplete canonical roundtrip for $command`: $work"
    }
    "TEXTURE_CANONICAL_ROUNDTRIP_OK configuration=$Configuration command=$command count=$($data.identical)"
}
# Same-codec canonical byte/float preservation; not an independent decoder comparison.
"TEXTURE_CANONICAL_ROUNDTRIP_ROOT=$work"
exit 0
