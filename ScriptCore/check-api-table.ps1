# 네이티브 ScriptApiTable과 C# ScriptApiTable의 필드 순서가 같은지 검사한다.
#
# 버전과 구조체 크기만으로는 순서가 뒤바뀐 것을 잡지 못한다. 실제로 필드 하나가
# 표 끝으로 밀렸는데 개수와 크기가 그대로라 검사를 통과했고, 그 결과 관리 코드가
# 엉뚱한 함수 포인터를 불러 접근 위반으로 죽었다. 원인을 찾는 데 한참 걸렸다.
#
# 사용법: pwsh ScriptCore\check-api-table.ps1
# 어긋나면 종료 코드 1과 함께 첫 불일치 지점을 보여 준다.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$nativePath = Join-Path $root 'Engine\SceneRuntime\ClrHost.cpp'
$managedPath = Join-Path $root 'ScriptCore\Native.cs'

function Get-NativeOrder {
    param([string]$Path)

    $lines = Get-Content -LiteralPath $Path
    $inTable = $false
    $names = New-Object System.Collections.Generic.List[string]

    foreach ($line in $lines) {
        if (-not $inTable) {
            if ($line -match '^\s*struct ScriptApiTable') { $inTable = $true }
            continue
        }
        if ($line -match '^\s*\};') { break }

        # 예: Float3 (__stdcall* Transform_GetForward)(ScriptObjectHandle handle);
        if ($line -match '\(\s*__stdcall\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)') {
            $names.Add($Matches[1])
        }
    }
    return $names
}

function Get-ManagedOrder {
    param([string]$Path)

    $lines = Get-Content -LiteralPath $Path
    $inTable = $false
    $names = New-Object System.Collections.Generic.List[string]

    foreach ($line in $lines) {
        if (-not $inTable) {
            if ($line -match '^\s*internal unsafe struct ScriptApiTable') { $inTable = $true }
            continue
        }
        if ($line -match '^\}') { break }

        # 예: public delegate* unmanaged<ObjectHandle, Float3> Transform_GetForward;
        if ($line -match 'delegate\*\s*unmanaged<[^>]*>\s+([A-Za-z_][A-Za-z0-9_]*)\s*;') {
            $names.Add($Matches[1])
        }
    }
    return $names
}

$native = Get-NativeOrder -Path $nativePath
$managed = Get-ManagedOrder -Path $managedPath

Write-Output "네이티브 $($native.Count)개 · 관리 $($managed.Count)개"

$limit = [Math]::Max($native.Count, $managed.Count)
for ($i = 0; $i -lt $limit; $i++) {
    $n = if ($i -lt $native.Count) { $native[$i] } else { '(없음)' }
    $m = if ($i -lt $managed.Count) { $managed[$i] } else { '(없음)' }

    if ($n -ne $m) {
        Write-Output ''
        Write-Output "[$i] 첫 불일치"
        Write-Output "  네이티브: $n"
        Write-Output "  관리:     $m"
        Write-Output ''
        Write-Output '두 ScriptApiTable의 필드 순서가 정확히 같아야 한다.'
        Write-Output '(버전·크기 검사는 순서가 뒤바뀐 것을 잡지 못한다)'
        exit 1
    }
}

Write-Output 'API 표 순서 일치'
exit 0
