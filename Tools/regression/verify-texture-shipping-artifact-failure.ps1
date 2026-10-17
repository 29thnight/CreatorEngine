param(
    [string]$PackageRoot,
    [string]$ContentRoot,
    [int]$TimeoutSeconds = 600
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$PackageRoot) {
    $receipt = Get-Content (Join-Path $repo 'Build/Phase12CoreVerification/result.json') -Raw | ConvertFrom-Json
    $PackageRoot = $receipt.stage
}
if (!$ContentRoot) {
    throw 'ContentRoot must name a retained, source-image-free Merged fixture with CEMF and CECT.'
}
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$ContentRoot = (Resolve-Path -LiteralPath $ContentRoot).Path
$manifest = Get-Content (Join-Path $PackageRoot 'package-manifest.json') -Raw | ConvertFrom-Json
if (!$manifest.shipping -or $manifest.verification -ne 'passed') { throw 'A verified Shipping baseline is required.' }
if (@(Get-ChildItem (Join-Path $ContentRoot 'Assets') -Recurse -File | Where-Object Extension -Match '^\.(png|jpg|jpeg|hdr|dds)$').Count) {
    throw 'Fixture contains original images.'
}
$runtime = Join-Path $repo 'Bin/x64-Release-Shipping/Player/Player.runtime.dll'
if ((Get-FileHash $runtime).Hash -ne (Get-FileHash (Join-Path $PackageRoot 'Player.runtime.dll')).Hash) {
    throw 'Shipping baseline is stale.'
}
$originals = @()
foreach ($entry in $manifest.runtimeEntries) {
    if ((Get-FileHash (Join-Path $PackageRoot $entry.path)).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Baseline runtime differs from manifest: $($entry.path)"
    }
    $originals += [pscustomobject]@{ path = Join-Path $PackageRoot $entry.path; hash = (Get-FileHash (Join-Path $PackageRoot $entry.path)).Hash }
}
$originals += [pscustomobject]@{ path = Join-Path $PackageRoot 'GameAssets.pak'; hash = (Get-FileHash (Join-Path $PackageRoot 'GameAssets.pak')).Hash }
if ($originals[-1].hash.ToLowerInvariant() -ne $manifest.pakFileSha256) { throw 'Baseline PAK differs from manifest.' }
$inputs = @(Get-ChildItem -LiteralPath $ContentRoot -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName; hash = (Get-FileHash -LiteralPath $_.FullName).Hash }
})
$case = Join-Path $repo ('Build/Obj/TextureShippingFailure/' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $case 'Player'
$ownedContent = Join-Path $case 'Content'
New-Item -ItemType Directory -Path $stage,$ownedContent -Force | Out-Null
Copy-Item -LiteralPath $PackageRoot -Destination $stage -Recurse
# Copy-Item creates the source directory beneath the existing destination.
$stage = Join-Path $stage (Split-Path $PackageRoot -Leaf)
Copy-Item -Path (Join-Path $ContentRoot '*') -Destination $ownedContent -Recurse
$textures = @(Get-ChildItem (Join-Path $ownedContent 'Assets/Derived/Textures') -Recurse -Filter '*.cetex' -File)
if ($textures.Count -ne 2) { throw 'Expected the PNG/HDR two-texture core fixture.' }
$packer = Join-Path $repo 'Bin/x64-Release/Tools/AssetPacker/AssetPacker.exe'
if (!(Test-Path $packer)) { throw "AssetPacker missing: $packer" }
$results = @()
function Invoke-Case([string]$Name, [bool]$ExpectedFailure) {
    $pak = Join-Path $stage 'GameAssets.pak'
    $pack = @(& $packer --assets (Join-Path $ownedContent 'Assets') --settings (Join-Path $ownedContent 'ProjectSetting') --output $pak --list-entries 2>&1)
    $packExit = $LASTEXITCODE
    $pack | Set-Content (Join-Path $case ($Name + '-pack.log')) -Encoding utf8
    if ($packExit -ne 0) { throw "Packing failed: $Name" }
    $stdout = Join-Path $case ($Name + '.stdout.log')
    $stderr = Join-Path $case ($Name + '.stderr.log')
    $before = (Get-FileHash $pak).Hash
    $process = Start-Process -FilePath (Join-Path $stage 'Player.exe') -ArgumentList @('--smoke','120','--smoke-promotions','120','--smoke-offscreen') `
        -WorkingDirectory $stage -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Player timeout: $Name"
    }
    $process.WaitForExit()
    $log = (Get-Content $stdout -Raw) + (Get-Content $stderr -Raw)
    if ($ExpectedFailure) {
        if ($process.ExitCode -ne 161 -or $log -notmatch 'Packaged cooked catalog mount failed: CEMF source asset이 package에 없다:') {
            throw "Wrong failure: $Name exit=$($process.ExitCode)"
        }
    } else {
        if ($process.ExitCode -ne 0 -or $log -notmatch '\[lx.scene.program\] source=cooked .*sceneCompiles=0' -or
            $log -notmatch 'GPUValidation=on' -or $log -notmatch '\[player.service\] compiled=no enabled=no' -or
            $log -notmatch '\[runtime.text-parser\] calls=0') {
            throw "Restored smoke failed: exit=$($process.ExitCode)"
        }
        $rows = @($log -split '\r?\n' | Where-Object { $_.StartsWith('[player.smoke] ') })
        if ($rows.Count -ne 1) {
            throw 'Restored smoke completion counters missing.'
        }
        $readiness = $rows[0].Substring(15) | ConvertFrom-Json
        if (!$readiness.ready -or $readiness.frames -lt 120 -or $readiness.displayPromotions -lt 120 -or
            $readiness.submittedGameFrames -le 0 -or
            $log -notmatch 'pending task/batch/retirement 0/0/0' -or
            $log -notmatch '\[PresentationThread\] shutdown[^\r\n]*pending 0 / balanced 1' -or
            $log -notmatch '\[RenderThread\] shutdown drain[^\r\n]*pending 0[^\r\n]*balanced 1') {
            throw 'Restored readiness or shutdown balance failed.'
        }
        $readiness | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $case 'restored-readiness.json') -Encoding utf8
    }
    if ((Get-FileHash $pak).Hash -ne $before) { throw "Player changed PAK: $Name" }
    $script:results += [pscustomobject]@{ name = $Name; exitCode = $process.ExitCode; expectedFailure = $ExpectedFailure;
        pakSha256 = $before; stdout = $stdout; stderr = $stderr; pakPreserved = $true }
    "CASE_OK name=$Name exit=$($process.ExitCode)"
}
$previousValidation = $env:CREATOR_DX12_VALIDATION
try {
    $env:CREATOR_DX12_VALIDATION = 'gpu'
    foreach ($texture in $textures) {
        $bytes = [IO.File]::ReadAllBytes($texture.FullName)
        foreach ($mode in @('missing','truncated')) {
            $leaf = $texture.BaseName + '-' + $mode
            $target = [IO.Path]::GetFullPath($texture.FullName)
            if (!$target.StartsWith($ownedContent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Mutation escaped owned fixture.'
            }
            try {
                if ($mode -eq 'missing') { Remove-Item -LiteralPath $target }
                else { [IO.File]::WriteAllBytes($target, [byte[]]@(67,69,67,84)) }
                Invoke-Case $leaf $true
            } finally {
                [IO.File]::WriteAllBytes($target, $bytes)
            }
        }
    }
    Invoke-Case 'restored' $false
} finally {
    $env:CREATOR_DX12_VALIDATION = $previousValidation
}
foreach ($entry in @($originals) + @($inputs)) {
    if ((Get-FileHash -LiteralPath $entry.path).Hash -ne $entry.hash) { throw "Baseline changed: $($entry.path)" }
}
foreach ($entry in $manifest.runtimeEntries) {
    if ((Get-FileHash (Join-Path $stage $entry.path)).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Test runtime changed: $($entry.path)"
    }
}
[ordered]@{ baseline = $PackageRoot; fixture = $ContentRoot; case = $case; results = $results;
    baselineFilesPreserved = $originals.Count; fixtureFilesPreserved = $inputs.Count;
    playerRuntimeSha256 = (Get-FileHash $runtime).Hash; scope = 'missing/truncated standalone CECT mount rejection; restored Shipping smoke';
    sameSizePayloadCorruption = 'not tested'; gpuPixelOracle = $false } |
    ConvertTo-Json -Depth 6 | Set-Content (Join-Path $case 'result.json') -Encoding utf8
$case | Set-Content (Join-Path $repo 'Build/Phase12CoreVerification/artifact-failure-root.txt') -Encoding utf8
"TEXTURE_SHIPPING_ARTIFACT_FAILURE_OK negatives=4 restored=1 baselinePreserved=true case=$case"
