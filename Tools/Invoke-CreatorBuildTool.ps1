# Source-checkout compatibility adapter. Product installs invoke CreatorBuildTool.exe directly.
function Invoke-CreatorBuildTool {
    param([string]$Command, [System.Collections.IDictionary]$Parameters, [string]$Repository)
    $config = if ($Parameters.Contains('Config')) { [string]$Parameters['Config'] } else { 'Debug' }
    $root = if ($Parameters.Contains('EngineDistribution') -and $Parameters['EngineDistribution']) {
        [IO.Path]::GetFullPath([string]$Parameters['EngineDistribution'])
    } else { $Repository }
    $tool = Join-Path $root "Bin/x64-$config/Tools/CreatorBuildTool/CreatorBuildTool.exe"
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "CreatorBuildTool.exe is missing: $tool. Build BuildTool/CreatorBuildTool.csproj first."
    }
    $toolArguments = [Collections.Generic.List[string]]::new()
    $toolArguments.Add($Command)
    foreach ($key in $Parameters.Keys) {
        $value = $Parameters[$key]
        if ($value -is [System.Management.Automation.SwitchParameter] -or $value -is [bool]) {
            if ([bool]$value) { $toolArguments.Add('-' + $key) }
        } elseif ($null -ne $value -and [string]$value -ne '') {
            $toolArguments.Add('-' + $key)
            $toolArguments.Add([string]$value)
        }
    }
    & $tool @toolArguments
    if ($LASTEXITCODE -ne 0) { throw "CreatorBuildTool $Command failed ($LASTEXITCODE)." }
}
