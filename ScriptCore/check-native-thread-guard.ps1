# Native 진입 검사 정적 대조 (LC5-c · 2026-09-05).
#
# ── 왜 정적 게이트가 필요한가 ──
#
# 게임 스레드 검사는 Native의 정적 메서드 206곳에 하나씩 들어가 있다. 그 규약을
# 사람이 지키게 두면 곧 드리프트한다 — 새 엔진 API를 하나 더하면서 검사를
# 빠뜨려도 컴파일은 되고, 실행 게이트(verify-lifecycle-thread)는 자기가 부르는
# 두 API만 보므로 나머지 204곳이 새는 것을 못 본다.
#
# 그래서 "빠진 곳이 있는가"는 실행이 아니라 소스에서 센다.
#
# ── 판정 ──
#
#   1 전수 배선   _api 를 만지는 메서드는 전부 Entered() 를 거친다
#   2 우회 없음   _bound 원본 필드는 허용된 자리에서만 읽힌다
#   3 검사 실질   Entered 가 실제로 스레드를 비교하고 경계 밖을 보고한다
#
# 판정 2가 없으면 게이트를 통과시키는 가장 쉬운 길이 검사를 넣는 것이 아니라
# _bound 를 직접 읽는 것이 된다. 판정 3이 없으면 Entered 의 속을 비워도 1·2가
# 초록으로 남는다 — 이름만 맞으면 통과하는 게이트가 된다.
#
# 사용법: pwsh ScriptCore\check-native-thread-guard.ps1 [-Configuration Release]
#         -GeneratedDirectory 로 빌드와 같은 생성 디렉터리를 지정할 수 있다.
[CmdletBinding()]
param(
    [string]$GeneratedDirectory = '',
    [string]$Declarations = '',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [ValidateSet('true', 'false')][string]$EngineShipping = 'false'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($GeneratedDirectory))
{
    $configurationKey = $Configuration
    if ($EngineShipping -eq 'true')
    {
        $configurationKey += '-Shipping'
    }
    $GeneratedDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) "Build/Generated/ScriptBindings/$Platform-$configurationKey"
}
$nativePath = Join-Path $PSScriptRoot 'Native.cs'
$generatedPath = Join-Path $GeneratedDirectory 'Native.Light.g.cs'
foreach ($path in @($nativePath, $generatedPath))
{
    if (-not (Test-Path -LiteralPath $path -PathType Leaf))
    {
        throw "Missing Native source: $path. Generate through the approved SceneRuntime/ScriptCore build first (Tools/ScriptBindings/BUILD.md), or supply -GeneratedDirectory. This checker never runs generation."
    }
}

# The same read-only preflight checks all seven outputs and their input/output
# hashes, including the declaration manifest. It never builds or generates.
& (Join-Path $PSScriptRoot 'check-api-table.ps1') -GeneratedDirectory $GeneratedDirectory `
    -Declarations $Declarations -Configuration $Configuration -Platform $Platform -EngineShipping $EngineShipping
if ($LASTEXITCODE -ne 0)
{
    throw 'Generated API table preflight failed; thread-guard coverage cannot be trusted.'
}

# 진단이 나가는 통로 자체는 면제한다. Log가 자기 검사에 막히면 거부가 조용해져
# 저작자에게는 "왜 갑자기 빈 값이지"만 남는다. 실제 표 호출은 PrintLog가 한다.
# 둘을 재검사하면 ReportOffThread → Log → PrintLog에서 같은 진단으로 재진입한다.
$exempt = @('Log', 'PrintLog')

# _bound 원본을 읽어도 되는 자리. 표 연결 그 자체를 다루거나(Bind·IsReady),
# 검사의 구현이거나(Entered), 면제 대상(Log)이다.
$boundAllowed = @('Bind', 'IsReady', 'Entered', 'Log', 'PrintLog')

function Get-NativeMembers
{
    param([string]$Path)

    $text = Get-Content -LiteralPath $Path -Raw
    # 주석 속 Entered()는 검사로 세지 않는다. 문자열의 //는 주석으로 지우지 않는다.
    $text = [regex]::Replace($text, '"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\r\n]*', {
        param($match)
        if ($match.Value.StartsWith('"', [StringComparison]::Ordinal))
        {
            return $match.Value
        }
        return ($match.Value -replace '[^\r\n]', ' ')
    })
    $pattern = '(?ms)^(?<indent>[ \t]*)internal\s+static\s+unsafe\s+(?:partial\s+)?class\s+Native\s*\{(?<body>.*?)^\k<indent>\}'
    $classes = [regex]::Matches($text, $pattern)
    if ($classes.Count -ne 1)
    {
        throw "Expected one complete Native class body in $Path, found $($classes.Count)."
    }
    $lines = $classes[0].Groups['body'].Value -split "`r?`n"
    $indent = [regex]::Escape($classes[0].Groups['indent'].Value + '    ')
    $starts = @()
    for ($i = 0; $i -lt $lines.Count; ++$i)
    {
        if ($lines[$i] -match ('^' + $indent + '(public|private|internal) static '))
        {
            $starts += $i
        }
    }
    $starts += $lines.Count
    for ($k = 0; $k -lt $starts.Count - 1; ++$k)
    {
        $a = $starts[$k]
        $b = $starts[$k + 1]
        $decl = $lines[$a]
        $name = if ($decl -match '\b(\w+)\s*(\(|=>|\{)') { $Matches[1] } else { $decl.Trim() }
        # 선언뿐인 _bound는 읽기가 아니다. 초기화식이 있는 필드는 검사한다.
        $isField = ($decl -notmatch '[(]' -and $decl -notmatch '=>' -and $decl -notmatch '\{' -and $decl -notmatch '=')
        [pscustomobject]@{
            Name    = $name
            IsField = $isField
            Body    = ($lines[$a..($b - 1)] -join "`n")
            Path    = $Path
        }
    }
}

$baseMembers = @(Get-NativeMembers $nativePath)
$generatedMembers = @(Get-NativeMembers $generatedPath)
if ($baseMembers.Count -lt 100)
{
    throw "Native.cs static member count $($baseMembers.Count) is below 100; parsing coverage has collapsed."
}
$members = @($baseMembers) + @($generatedMembers)
$failed = New-Object System.Collections.Generic.List[string]

# 생성된 13개 helper는 허용된 짧은 형식 전체를 검사한다. 이름만 포함한 주석,
# null 검사 삭제, ||로 바꾼 조건, 검사 전/후의 별도 호출, _bound 우회는 통과하지 못한다.
$lightFields = @(
    'Exists', 'GetColor', 'SetColor', 'GetIntensity', 'SetIntensity', 'GetRange', 'SetRange',
    'GetSpotAngle', 'SetSpotAngle', 'GetLightType', 'SetLightType', 'GetLightStatus', 'SetLightStatus'
)
if ($generatedMembers.Count -ne 13)
{
    "생성 Native.Light.g.cs 멤버 $($generatedMembers.Count)개 (기대 13)"
    $failed.Add('생성(범위)')
}
foreach ($field in $lightFields)
{
    $name = "Light$field"
    $helpers = @($generatedMembers | Where-Object { $_.Name -ceq $name })
    if ($helpers.Count -ne 1 -or @($baseMembers | Where-Object { $_.Name -ceq $name }).Count -ne 0)
    {
        "생성 helper $name 정의가 없거나 중복됐다."
        $failed.Add("생성($name)")
        continue
    }
    $body = ($helpers[0].Body -replace '\s+', ' ').Trim()
    $prefix = '^public static \w+ ' + [regex]::Escape($name) + '\(ObjectHandle handle'
    $slot = [regex]::Escape("_api.Light.$field")
    if ($field -ceq 'Exists')
    {
        $guarded = $prefix + '\) => Entered\(\) && ' + $slot + ' != null && ' + $slot + '\(handle\) != 0;$'
    }
    elseif ($field.StartsWith('Get', [StringComparison]::Ordinal))
    {
        $guarded = $prefix + '\) => Entered\(\) && ' + $slot + ' != null \? ' + $slot + '\(handle\) : (?:Color4\.White|0f|0);$'
    }
    else
    {
        $guarded = $prefix + ', \w+ value\) \{ if \(Entered\(\) && ' + $slot + ' != null\) \{ ' + $slot + '\(handle, value\); \} \}$'
    }
    if ($body -cnotmatch $guarded)
    {
        "생성 helper $name 의 Entered()/null/호출 순서가 보장되지 않는다."
        $failed.Add("생성($name)")
    }
}

# ── 판정 1: _api 를 만지는 메서드는 전부 Entered() 를 거친다 ──────────────────

$apiMembers = @($members | Where-Object { $_.Body -match '_api\.' -and $exempt -notcontains $_.Name })
$unguarded = @($apiMembers | Where-Object { $_.Body -notmatch 'Entered\(\)' })

"판정 1 전수 배선: _api 사용 $($apiMembers.Count) 곳 중 검사 없는 곳 $($unguarded.Count) 개 (기대 0)"
if ($apiMembers.Count -lt 100) {
    "  → _api 사용 메서드가 $($apiMembers.Count) 개뿐이다. 세는 범위가 무너졌다."
    $failed.Add('1(범위)')
}
if ($unguarded.Count -gt 0) {
    foreach ($m in $unguarded) { "    $($m.Name)" }
    "  → 이 메서드들은 게임 스레드 밖에서도 그대로 C++로 들어간다."
    "  → 검사를 넣거나, 면제해야 할 이유가 있으면 이 스크립트의 `$exempt 에 근거와 함께 적어라."
    $failed.Add('1')
}

# ── 판정 2: _bound 원본은 허용된 자리에서만 읽힌다 ────────────────────────────

$boundLeak = @($members | Where-Object {
    -not $_.IsField -and $_.Body -match '_bound' -and $boundAllowed -notcontains $_.Name
})

"판정 2 우회 없음: 허용 밖에서 _bound 를 읽는 곳 $($boundLeak.Count) 개 (기대 0)"
if ($boundLeak.Count -gt 0) {
    foreach ($m in $boundLeak) { "    $($m.Name)" }
    "  → 검사를 건너뛰는 우회로다. 판정 1은 이것을 잡지 못한다 — Entered 를 부르지 않고도"
    "     표 연결 여부만 보고 C++에 들어갈 수 있기 때문이다."
    $failed.Add('2')
}

# ── 판정 3: 검사가 실제로 무언가를 한다 ───────────────────────────────────────

$entered = @($members | Where-Object { $_.Name -eq 'Entered' })
"판정 3 검사 실질: Entered 정의 $($entered.Count) 개 (기대 1)"
if ($entered.Count -ne 1) {
    "  → 검사의 정의를 찾지 못했다. 판정 1·2는 이름만 보므로 이것 없이는 무의미하다."
    $failed.Add('3(정의)')
}
else {
    $body = $entered[0].Body
    $needs = @(
        @{ Pattern = '_gameThreadId';               Why = '게임 스레드 id와 비교' },
        @{ Pattern = 'CurrentManagedThreadId';      Why = '현재 스레드 조회' },
        @{ Pattern = 'ReportOffThread';             Why = '경계 밖 보고' },
        @{ Pattern = 'return false';                Why = '거부' }
    )
    $lost = @($needs | Where-Object { $body -notmatch [regex]::Escape($_.Pattern) })
    if ($lost.Count -gt 0) {
        foreach ($n in $lost) { "    빠짐: $($n.Pattern) — $($n.Why)" }
        "  → 속이 빈 검사다. 206곳이 그것을 부르고 있어도 아무것도 막지 않는다."
        $failed.Add('3')
    }
}

""
if ($failed.Count -gt 0) {
    "붉은 판정: $($failed -join ', ')"
    exit 1
}

"전체 통과 — 엔진 API $($apiMembers.Count) 곳이 전부 게임 스레드 검사를 거친다"
exit 0
