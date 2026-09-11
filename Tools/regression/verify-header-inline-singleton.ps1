# 헤더 인라인 Meyers 싱글턴 금지 (PlayerModuleBoundaryAnalysis Step 0)
#
# 무엇을 지키는가
# ─────────────
# 엔진·에디터·Player 헤더에 "함수 안 static 지역 변수를 두고 그 참조를 돌려주는"
# 정의가 없는지 본다. 정의는 .cpp로 내려가야 하고 헤더에는 선언만 남아야 한다.
#
# 왜 필요한가
# ──────────
# static 지역 변수의 COMDAT folding은 **하나의 PE 안에서만** 일어난다. 지금은 전
# 모듈이 StaticLibrary라 exe 하나로 접히지만, 모듈 경계를 로드 단위로 바꾸는 순간
# (엔진 DLL화 — 축 B) 헤더를 include한 쪽마다 별도 인스턴스가 생긴다. 그러면 한쪽이
# 등록한 리사이즈 구독자나 진행률 싱크를 다른 쪽 브로드캐스트가 못 보는, 크래시도
# 로그도 없는 조용한 결함이 된다.
#
# 이 결함 계열은 실제로 6개 페이즈 동안 결합 인벤토리에서 누락돼 있었다
# (docs/analysis/PlayerModuleBoundaryAnalysis.md §4.2·§10.4). 규약을 주석으로만
# 적으면 다음 사람이 같은 자리에 또 만든다 — 도는 검사만이 강제력을 갖는다.
#
# ⚠ 통과가 곧 검증은 아니다
# ────────────────────────
# 이 검사는 "패턴이 없다"를 단정한다. 헤더를 못 찾거나 정규식이 빗나가도 0건이
# 나오므로 아래에서 둘을 먼저 확인한다:
#   ① 검사 대상 헤더가 실제로 모여 있는가 (빈 집합 위에서 도는 부재 단정 방지)
#   ② 정규식이 이 결함을 정말 잡는가 (합성 표본으로 자기 검사)
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

$failures = @()

# ── 검사 범위: 정적 링크로 exe에 들어가는 트리 전부 ──
$sourceRoots = @(
    (Join-Path $repoRoot "Engine"),
    (Join-Path $repoRoot "Editor"),
    (Join-Path $repoRoot "Player")
)
foreach ($root in $sourceRoots) {
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "검사 대상 루트가 없다 — 부재 단정이 빈 집합 위에서 돈다: $root"
    }
}

$headers = @(Get-ChildItem -LiteralPath $sourceRoots -Recurse -File -Include *.h, *.hpp `
    -ErrorAction Stop | Where-Object { $_.FullName -notmatch '\\(ThirdParty|vcpkg_installed|Build|x64)\\' })

# ── 양성 확인 ①: 대상이 실재한다 ──
if ($headers.Count -lt 300) {
    $failures += "검사한 헤더가 $($headers.Count)개뿐이다 — 범위가 무너졌을 수 있다(기대: 300개 이상)."
}

# 함수 시그니처가 참조를 돌려주고, 본문 안에 static 저장 기간 지역 변수가 있는 형태.
#   static T& Get() { static T instance; return instance; }      ← 클래스 정적 멤버
#   inline T& GetSink() { static T s_sink; return s_sink; }       ← 자유 인라인 함수
# static_cast는 \bstatic\b에 걸리지 않는다(뒤따르는 _가 단어 문자).
$pattern = '(?m)^[^\S\r\n]*(?:static|inline)\s+[^;{}()]{1,90}&\s*([A-Za-z_]\w*)\s*\([^)]*\)[^;{}]{0,40}\{[^{}]{0,400}?\bstatic\b'

# ── 양성 확인 ②: 정규식이 이 결함을 실제로 잡는다 ──
$probes = @(
    'static Foo& Get() { static Foo instance; return instance; }',
    "inline Sink& GetSink()`r`n{`r`n    static Sink s_sink;`r`n    return s_sink;`r`n}",
    "    static Bar& Get() noexcept`r`n    {`r`n        static Bar instance;`r`n        return instance;`r`n    }"
)
for ($i = 0; $i -lt $probes.Count; $i++) {
    if ($probes[$i] -notmatch $pattern) {
        $failures += "자기 검사 실패: 표본 $i 를 정규식이 못 잡는다 — 이 검사는 아무것도 지키지 못한다."
    }
}
# 음성 표본: 선언만 있는 형태와 static_cast는 잡히면 안 된다(거짓 양성 방지).
$negatives = @(
    'static Foo& Get();',
    'inline Sink& GetSink();',
    'inline int Clamp(int v) { return static_cast<int>(v); }'
)
for ($i = 0; $i -lt $negatives.Count; $i++) {
    if ($negatives[$i] -match $pattern) {
        $failures += "자기 검사 실패: 음성 표본 $i 를 거짓 양성으로 잡는다."
    }
}

# ── 허용 목록 ──
#
# 여기 넣으려면 "그 헤더를 include하는 모든 TU가 같은 모듈 안에 있다"를 근거로
# 적어야 한다. 소비가 모듈 밖으로 한 번이라도 나가면 허용 대상이 아니다.
$allowed = @{
    'Engine\EngineDiagnostics\Profiler.h' =
        'GetTLSUnsafe — EngineDiagnostics 내부 private 소비뿐이라 엔진 DLL 1개 안에 갇힌다(§10.4).'
    'Editor\EngineEntry\EditorSessionState.h' =
        'Get — 소비가 Editor\EngineEntry·EngineGUIWindow뿐이고 Player는 쓰지 않는다. 축 B에서 Editor는 호스트셸(exe)에 남으므로 경계를 넘지 않는다.'
    'Editor\EngineGUIWindow\ReflectionImGuiHelper.h' =
        'TypedDraw::Registry — 소비가 EngineGUIWindow뿐이다(SceneRuntime/Entity.h의 언급은 주석 한 줄). 위와 같은 이유로 exe 내부.'
}

$violations = @()
foreach ($header in $headers) {
    $text = Get-Content -LiteralPath $header.FullName -Raw -Encoding UTF8
    if ([string]::IsNullOrEmpty($text)) { continue }

    $matches = [regex]::Matches($text, $pattern)
    if ($matches.Count -eq 0) { continue }

    $relative = $header.FullName.Substring($repoRoot.Length).TrimStart('\')
    if ($allowed.ContainsKey($relative)) { continue }

    foreach ($m in $matches) {
        $line = ($text.Substring(0, $m.Index) -split "`n").Count
        $violations += "  $relative : $line  →  $($m.Groups[1].Value)()"
    }
}

if ($violations.Count -gt 0) {
    $failures += "헤더 인라인 Meyers 싱글턴 $($violations.Count)건:`n" + ($violations -join "`n") + `
        "`n  정의를 .cpp로 내리고 헤더에는 선언만 남길 것 (근거: docs/analysis/PlayerModuleBoundaryAnalysis.md §4.2)."
}

# 허용 목록이 낡지 않았는지 — 등재한 파일이 사라졌거나 이미 고쳐졌으면 알린다.
foreach ($entry in $allowed.Keys) {
    $path = Join-Path $repoRoot $entry
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $failures += "허용 목록의 $entry 가 없다 — 목록을 정리할 것."
        continue
    }
    $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    if ($text -notmatch $pattern) {
        $failures += "허용 목록의 $entry 가 이미 패턴을 갖지 않는다 — 목록에서 뺄 것(죽은 예외)."
    }
}

if ($failures.Count -gt 0) {
    Write-Host "[FAIL] verify-header-inline-singleton" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    exit 1
}

Write-Host "[PASS] verify-header-inline-singleton — 헤더 $($headers.Count)개, 위반 0건 (허용 $($allowed.Count)건)" -ForegroundColor Green
exit 0
