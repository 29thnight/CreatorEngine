param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) {
    throw 'Use a new artifact directory; previous evidence is preserved'
}
New-Item -ItemType Directory $root | Out-Null
$sources = @(Get-ChildItem "$repo/Engine", "$repo/Editor", "$repo/Tools/regression", "$repo/Dynamic_CPP/Assets/Shaders/DefaultPassShader" -Recurse -File |
    Where-Object Extension -In '.cpp', '.h', '.inl', '.slang', '.vcxproj', '.ps1') | Sort-Object FullName -Unique
$hashes = @($sources | ForEach-Object {
    [pscustomobject]@{path=$_.FullName; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$hashes | ConvertTo-Json -Depth 4 | Set-Content "$root/source-hashes.json" -Encoding utf8
$commands = "$root/commands.txt"
[IO.File]::WriteAllText($commands, "dx12.rendergraph`ndx12.validation`nquit`n")
$exe = "$repo/Bin/x64-$Configuration/Editor/CreatorEditor.exe"
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$runtime = Join-Path (Split-Path $exe) 'CreatorEditor.runtime.dll'
$runtimeHash = (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash
$previousPath = $env:PATH
$previousValidation = $env:CREATOR_DX12_VALIDATION
try {
    $dep = if ($Configuration -eq 'Debug') {'debug/bin'} else {'bin'}
    $env:PATH = "$repo/vcpkg_installed/x64-windows/$dep;$(Split-Path $exe);$previousPath"
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    $proc = Start-Process $exe -ArgumentList @('--development-project', "$repo/Build/Obj/RenderRG4/current-fixture/Project", '--commandlet-script', $commands, '--result-file', "$root/results.jsonl") -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root/stdout.log" -RedirectStandardError "$root/stderr.log"
    if (!$proc.WaitForExit(600000)) {
        $proc.Kill()
        throw 'Screen native test timed out'
    }
    $rows = @(Get-Content "$root/results.jsonl" | ForEach-Object {$_ | ConvertFrom-Json})
    if ($proc.ExitCode -ne 0 -or $rows.Count -ne 3 -or $rows[0].status -ne 'succeeded' -or
        !$rows[0].data.log.Contains('RG5_SCREEN_GPU_OK policies=3 frames=48 maxError=0')) {
        throw 'Screen native GPU gate failed'
    }
    $validation = $rows[1].data
    if ($rows[1].status -ne 'succeeded' -or !$validation.layerEnabled -or $validation.mode -ne 'gpu' -or
        $validation.problems -ne 0 -or $validation.droppedMessages -ne 0) {
        throw 'Screen GPU validation must be enabled and clean'
    }
    foreach ($source in $hashes) {
        if ((Get-FileHash -LiteralPath $source.path -Algorithm SHA256).Hash -cne $source.sha256) {
            throw "Source changed: $($source.path)"
        }
    }
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -cne $exeHash) {
        throw 'Editor executable changed during the test'
    }
    if ((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -cne $runtimeHash) {
        throw 'Editor runtime changed during the test'
    }
    @{runtime=$runtime; runtimeSha256=$runtimeHash; executable=$exe; configuration=$Configuration; passed=$true; policies=3; frames=48; maxError=0; validationProblems=$validation.problems;
        executableSha256=$exeHash; exitCode=$proc.ExitCode; productDefault='DeclarationOrder'; rg5Complete=$false} |
        ConvertTo-Json -Depth 5 | Set-Content "$root/result.json" -Encoding utf8
    "RG5_SCREEN_GPU_OK $Configuration"
}
finally {
    $env:PATH = $previousPath
    $env:CREATOR_DX12_VALIDATION = $previousValidation
}