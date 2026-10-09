# Shared GCCE boundary for standalone cl/link probes that bypass MSBuild targets.
# Resolve existing outputs only; never build or execute the engine implicitly.
function Get-GCCEProbeSettings {
    param(
        [Parameter(Mandatory)][string]$Repository,
        [Parameter(Mandatory)][ValidateSet('Debug', 'Release')][string]$Configuration
    )
    $include = Join-Path $Repository 'ThirdParty/GCCE/include'
    $library = Join-Path $Repository "Build/Lib/x64-$Configuration/gcce.lib"
    $runtime = Join-Path $Repository "Bin/x64-$Configuration/Runtime/Common"
    foreach ($required in @((Join-Path $include 'gc/gc.hpp'), $library, (Join-Path $runtime 'gcce.dll'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "GCCE probe dependency is missing: $required. Build Engine/GCCE/GCCE.vcxproj for $Configuration x64 first."
        }
    }
    $checks = if ($Configuration -eq 'Debug') { 1 } else { 0 }
    return [pscustomobject]@{
        CompileArguments = '/DGCCE_SHARED /DGC_DEBUG_CHECKS=' + $checks + ' /external:I"' + $include + '" /external:W0'
        LinkArguments = '"' + $library + '"'
        RuntimeDirectory = $runtime
    }
}
