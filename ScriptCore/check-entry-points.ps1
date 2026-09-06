# 네이티브가 이름으로 찾는 관리 진입점이 실제로 C#에 있는지 검사한다.
#
# ── 무엇을 잡는가 ──
#
# ClrHost는 관리 어셈블리의 함수를 **문자열 이름으로** 찾는다(bind(L"CreateComponent")).
# 그래서 C# 쪽 이름을 바꾸면 컴파일도 링크도 통과하고, API 표 순서 검사도 통과한다 —
# 그 표는 함수 포인터의 배치를 보지 이름 결합을 보지 않기 때문이다. 어긋난 것은
# 런타임에야 드러나고, 그것도 "CLR 초기화 실패"라는 뭉뚱그린 형태로 나온다.
#
# 실제로 9-4 재작성에서 Behaviour → Component 개명이 이 결합을 일곱 군데 건드렸다.
# 그때는 손으로 대조했지만, 손으로 하는 대조는 다음 사람이 하지 않는다.
#
# ── 한쪽 방향만 실패로 본다 ──
#
# "네이티브가 찾는데 C#에 없다"만 실패다. 그 반대(C#에는 있는데 아무도 안 찾는다)는
# 정상이다 — 관리 측이 먼저 표면을 넓히고 네이티브가 나중에 쓰는 순서가 흔하고,
# 실제로 지금도 그런 진입점이 여럿 있다(GetFieldFloat2 등).
#
# 사용법: pwsh ScriptCore\check-entry-points.ps1
# 어긋나면 종료 코드 1과 함께 빠진 이름을 전부 보여 준다.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$nativePath  = Join-Path $root 'Engine\SceneRuntime\ClrHost.cpp'
$managedPath = Join-Path $root 'ScriptCore\Bootstrap.cs'

foreach ($p in @($nativePath, $managedPath)) {
    if (-not (Test-Path $p)) { throw "파일이 없다: $p" }
}

# 네이티브: bind(L"이름") 을 전부 모은다.
$native = New-Object System.Collections.Generic.HashSet[string]
foreach ($m in [regex]::Matches((Get-Content -LiteralPath $nativePath -Raw), 'bind\(L"([A-Za-z0-9_]+)"')) {
    [void]$native.Add($m.Groups[1].Value)
}

# 관리: [UnmanagedCallersOnly] 가 붙은 static 메서드 이름.
#
# EntryPoint 를 명시하면 그것이 실제 노출 이름이고, 없으면 메서드 이름이 그대로 쓰인다.
# 둘을 구분하지 않으면 EntryPoint 를 쓴 진입점을 통째로 놓친다.
$managed = New-Object System.Collections.Generic.HashSet[string]
$managedText = Get-Content -LiteralPath $managedPath -Raw
$pattern = '\[UnmanagedCallersOnly([^\]]*)\][\s\S]{0,200}?\bstatic\b[^(]*?([A-Za-z0-9_]+)\s*\('
foreach ($m in [regex]::Matches($managedText, $pattern)) {
    $attr = $m.Groups[1].Value
    $ep = [regex]::Match($attr, 'EntryPoint\s*=\s*"([A-Za-z0-9_]+)"')
    [void]$managed.Add($(if ($ep.Success) { $ep.Groups[1].Value } else { $m.Groups[2].Value }))
}

Write-Output "네이티브가 찾는 이름 $($native.Count)개 · C#이 노출하는 진입점 $($managed.Count)개"

# 한쪽이 통째로 비면 정규식이 형태 변화를 못 따라간 것이다 — 그 경우 "빠진 것 없음"이
# 나와 검사가 조용히 아무것도 안 하게 된다. 빈 집합을 통과로 읽지 않는다.
if ($native.Count -eq 0)  { Write-Output ''; Write-Output 'ClrHost.cpp에서 bind(L"...")를 하나도 찾지 못했다 — 이 검사의 패턴이 낡았다.'; exit 1 }
if ($managed.Count -eq 0) { Write-Output ''; Write-Output 'Bootstrap.cs에서 진입점을 하나도 찾지 못했다 — 이 검사의 패턴이 낡았다.'; exit 1 }

$missing = @($native | Where-Object { -not $managed.Contains($_) } | Sort-Object)

if ($missing.Count -gt 0) {
    Write-Output ''
    Write-Output "네이티브가 찾는데 C#에 없다 ($($missing.Count)개):"
    foreach ($name in $missing) { Write-Output "  $name" }
    Write-Output ''
    Write-Output 'ClrHost가 이 이름으로 관리 함수를 찾는다. 없으면 CLR 초기화가 실패한다'
    Write-Output '(bind 실패 시 return false — 스크립트 계층 전체가 뜨지 않는다).'
    Write-Output 'C# 쪽 이름을 바꿨다면 ClrHost.cpp의 bind 문자열도 같이 바꿔야 한다.'
    exit 1
}

Write-Output '진입점 결합 일치'
exit 0
