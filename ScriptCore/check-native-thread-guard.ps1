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
# 사용법: pwsh ScriptCore\check-native-thread-guard.ps1

$ErrorActionPreference = 'Stop'

$nativePath = Join-Path $PSScriptRoot "Native.cs"
if (-not (Test-Path $nativePath)) { "Native.cs가 없다: $nativePath"; exit 1 }

$text = Get-Content -LiteralPath $nativePath -Raw
$classAt = $text.IndexOf('internal static unsafe class Native')
if ($classAt -lt 0) { "Native 클래스 선언을 찾지 못했다 — 이 게이트의 전제가 깨졌다."; exit 1 }

$lines = ($text.Substring($classAt) -split "`r?`n")

# 진단이 나가는 통로 자체는 면제한다. Log가 자기 검사에 막히면 거부가 조용해져
# 저작자에게는 "왜 갑자기 빈 값이지"만 남는다.
$exempt = @('Log')

# _bound 원본을 읽어도 되는 자리. 표 연결 그 자체를 다루거나(Bind·IsReady),
# 검사의 구현이거나(Entered), 면제 대상(Log)이다.
$boundAllowed = @('Bind', 'IsReady', 'Entered', 'Log')

# 메서드 경계: 들여쓰기 4칸의 정적 멤버 선언.
$starts = @()
for ($i = 0; $i -lt $lines.Count; ++$i) {
    if ($lines[$i] -match '^    (public|private|internal) static ') { $starts += $i }
}
if ($starts.Count -lt 100) {
    "정적 멤버를 $($starts.Count) 개밖에 찾지 못했다 — 파싱 전제가 깨졌다(파일 구조 변경?)."
    exit 1
}
$starts += $lines.Count

$members = @()
for ($k = 0; $k -lt $starts.Count - 1; ++$k) {
    $a = $starts[$k]; $b = $starts[$k + 1]
    $decl = $lines[$a]
    $name = if ($decl -match '\b(\w+)\s*(\(|=>|\{)') { $Matches[1] } else { $decl.Trim() }
    # 필드 선언은 멤버 경계로는 잡히지만 무언가를 "읽지"는 않는다. 본문이 있는
    # 것만 판정 대상으로 삼는다 — 그러지 않으면 `private static bool _bound;`
    # 선언 자체가 우회로로 잡힌다.
    $isField = ($decl -notmatch '[(]' -and $decl -notmatch '=>' -and $decl -notmatch '\{')

    $members += [pscustomobject]@{
        Name    = $name
        IsField = $isField
        Body    = ($lines[$a..($b - 1)] -join "`n")
    }
}

$failed = New-Object System.Collections.Generic.List[string]

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
