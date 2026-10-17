$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$violations = @()
Push-Location $repo
try {
    $paths = @(& git -c core.longpaths=true ls-files --cached --others --exclude-standard -- '*.cpp' '*.h' '*.hpp' '*.inl' '*.ixx' '*.cppm' '*.cxx' '*.cc' ':(exclude)Artifacts/**' ':(exclude)Build/**' ':(exclude)Bin/**' ':(exclude)Dynamic_CPP/Saved/**' ':(exclude)ThirdParty/**' ':(exclude)vcpkg_installed/**')
    if ($LASTEXITCODE) {
        throw 'Could not enumerate owned source files'
    }
    foreach ($path in ($paths | Sort-Object -Unique)) {
        if ($path -match '^(ThirdParty|vcpkg_installed|Artifacts|Build|Bin|docs)/') {
            continue
        }
        $matches = @(Select-String -LiteralPath $path -Pattern '\bvector\s*<\s*bool\b')
        foreach ($match in $matches) {
            $violations += ('{0}:{1}' -f $path, $match.LineNumber)
        }
    }
    if ($violations.Count) {
        throw ('Packed bool vector is prohibited: ' + ($violations -join ', '))
    }
    Write-Output 'BIT_STORAGE_POLICY_OK owned source contains no packed bool vector'
}
finally {
    Pop-Location
}
