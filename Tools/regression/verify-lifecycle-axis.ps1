# 생명주기 축 게이트 — 편집/재생 경계 · 축소 전달 시점 · 활성 전파
#
# ★ 이 게이트는 지금 **붉은 것이 정상이다.** 결함을 증명하려고 세운 것이고,
#   고침이 착지하면 초록이 된다. 그때 run-all.ps1에 넣는다 — 지금 넣으면
#   세트가 항상 실패한다.
#
# ── 무엇을 메우는가 ──
#
# Tools\regression\lifecycle_baseline.tsv 223줄에 ScriptComponent 행이 **0건**이다.
# 네이티브 생명주기 기준선이 스크립트를 한 번도 태우지 않는다. verify-ddol-script는
# DDOL 이송만 덮는다. 그래서 아래 셋은 지금 어떤 게이트로도 붉어지지 않는다.
#
#   T1 편집 모드에서 6단계가 이미 돈다
#      EditorMain.cpp:510이 "편집 모드에서는 스크립트를 돌리지 않는다"고 적어
#      뒀지만, 그 return 앞의 SceneManagers->Editor()가 매 프레임
#      DrainPendingLifecycle을 돌린다(SceneManager.cpp:382, 그리고
#      SceneManager.cpp:1503이 "에디터 틱도 예외가 아니다"라고 명시한다).
#
#   T2 재생 정지에서 축소가 제때 오지 않는다
#      ScriptRegistry.Flush가 관리 틱 안에만 있고 편집 모드에는 관리 틱이 없다.
#      네이티브 축소는 DispatchLifecycle의 IsAlive 가드에 걸려 버려진다
#      (Entity::Destroy가 ScriptObjectRegistry::Unregister를 먼저 부른다).
#
#      ★ 그래서 증상이 "영영 안 온다"가 아니다. 프로세스 종료의
#        ScriptRegistry.Clear가 결국 TearDown을 태운다 — 즉 **개수만 세면 초록**이다.
#        판정 2b가 시점을 본다.
#
#   T3 오브젝트를 꺼도 스크립트가 계속 돈다
#      ScriptComponent가 OnEnable/OnDisable을 override하지 않아 활성 축이 경계에서
#      끊겨 있다(LifecycleRegistry의 마스크에 Bit_OnEnable/Bit_OnDisable이 서지
#      않는다). 틱 게이트는 관리 측 Component.Enabled만 본다.
#
#      ★ 여기서도 개수는 이빨이 없다. 종료 시 Clear가 OnDisable을 한 번 부르므로
#        "OnDisable >= 1"은 결함이 있어도 참이다. 판정 3은 **위치**를 본다 —
#        predisable 마크와 disabled 마크 사이에 있어야 한다.
#
# ── 왜 #1과 #2를 가르는가 ──
#
# script.add는 자기가 DrainPendingLifecycle을 동기로 부른다
# (ConsoleCommandSystem.cpp:6650). 그래서 #1이 편집 모드에서 훅을 받은 것은 그
# 명령 탓일 수 있어 원인 귀속이 오염돼 있다. 정지가 씬을 백업에서 되살릴 때 뜨는
# #2에는 어떤 명령도 관여하지 않는다 — 판정 1은 그 #2만 본다.
#
# ── 사용법 ──
#   pwsh Tools\regression\verify-lifecycle-axis.ps1
#   pwsh Tools\regression\verify-lifecycle-axis.ps1 -Exe <다른 빌드의 exe>
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$Work = $env:TEMP,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { "실행 파일이 없다: $Exe"; exit 1 }
$exeItem = Get-Item -LiteralPath $Exe
$exeDir = $exeItem.DirectoryName

# 어느 바이너리를 쟀는지 반드시 남긴다 — 낡은 exe를 재는 사고가 이 저장소에서
# 세 번 났다(구성이 다른 기본값 · 손상된 증분 링크 · Release/Debug 혼선).
"대상 exe   : $($exeItem.FullName)"
"빌드 시각  : $($exeItem.LastWriteTime)"

# 관리 스크립트 어셈블리도 함께 남긴다. 프로브는 C# 쪽에 있으므로 GameScripts.dll이
# 낡으면 이 게이트는 '없는 타입'을 붙이려다 조용히 아무것도 재지 못한다.
$gameScripts = Join-Path $exeDir "..\Managed\Scripts\GameScripts.dll"
if (Test-Path $gameScripts) {
    $gsItem = Get-Item -LiteralPath $gameScripts
    "GameScripts: $($gsItem.LastWriteTime)"
} else {
    "GameScripts: 찾지 못함($gameScripts) — 경로가 바뀌었는지 확인할 것"
}
""

$scenario = Join-Path $PSScriptRoot "lifecycle_axis_probe.txt"
if (-not (Test-Path $scenario)) { "시나리오가 없다: $scenario"; exit 1 }

$outPath = Join-Path $Work "lifecycle_axis.out"
$errPath = Join-Path $Work "lifecycle_axis.err"

$proc = Start-Process -FilePath $Exe -ArgumentList "--script", $scenario `
    -WorkingDirectory $exeDir `
    -RedirectStandardOutput $outPath `
    -RedirectStandardError $errPath -PassThru

$proc.WaitForExit($TimeoutSeconds * 1000) | Out-Null
if (-not $proc.HasExited) {
    $proc.Kill()
    "타임아웃 ($TimeoutSeconds 초). 시나리오가 끝나지 않았다."
    exit 1
}

$logDir = Join-Path $exeDir "Saved\Log"
$editorLog = @(Get-ChildItem (Join-Path $logDir "Editor_*.html") -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1)
if ($editorLog.Count -eq 0) { "에디터 로그를 찾지 못했다: $logDir\Editor_*.html"; exit 1 }

$logText = (Get-Content -LiteralPath $editorLog[0].FullName -Raw) -replace '<[^>]+>', ''

# ── 한 형식만 판다 ──
# [Axis] #<서수> kind=<hook|tick|mark> name=<이름> frame=<F> pre=<n> post=<m>
$rx = '\[Axis\] #(\d+) kind=(\w+) name=(\w+) frame=(\d+) pre=(\d+) post=(\d+)'
$events = @([regex]::Matches($logText, $rx) | ForEach-Object {
    [pscustomobject]@{
        Ordinal = [int]$_.Groups[1].Value
        Kind    = $_.Groups[2].Value
        Name    = $_.Groups[3].Value
        Frame   = [long]$_.Groups[4].Value
        Pre     = [int]$_.Groups[5].Value
        Post    = [int]$_.Groups[6].Value
    }
})

if ($events.Count -eq 0) {
    "[Axis] 로그가 한 줄도 없다 — 프로브가 붙지 않았다."
    "  · GameScripts를 빌드했는가(LifecycleAxisProbe)"
    "  · script.add가 성공했는가: $outPath 의 '[CLI] 부착' 줄 확인"
    exit 1
}

# 인스턴스별로 나눈다. 순서 판정은 반드시 이 좁힌 목록 안에서만 한다 —
# 전체에서 IndexOf를 하면 다른 인스턴스의 같은 이름 줄을 집는다.
$e1 = @($events | Where-Object { $_.Ordinal -eq 1 })
$e2 = @($events | Where-Object { $_.Ordinal -eq 2 })

function Get-MarkIndex {
    param($List, [string]$Kind, [string]$Name)
    for ($i = 0; $i -lt $List.Count; ++$i) {
        if ($List[$i].Kind -eq $Kind -and $List[$i].Name -eq $Name) { return $i }
    }
    return -1
}

# ── 눈으로 검산할 수 있게 통째로 찍는다 ──
"── #1 (script.add 로 붙인 인스턴스 · 원인 귀속 오염됨) ─────────────────"
$e1 | ForEach-Object { "  f{0,-8} {1,-5} {2,-20} pre={3,-5} post={4}" -f $_.Frame, $_.Kind, $_.Name, $_.Pre, $_.Post }
""
"── #2 (정지 후 백업 복원분 · 오염 없음) ────────────────────────────────"
if ($e2.Count -eq 0) { "  (없음)" }
else { $e2 | ForEach-Object { "  f{0,-8} {1,-5} {2,-20} pre={3,-5} post={4}" -f $_.Frame, $_.Kind, $_.Name, $_.Pre, $_.Post } }
""

$failed = @()

# ── 판정 0 (기준선) — 재생 중 틱이 실제로 돌았는가 ──
#
# 이것이 거짓이면 아래 판정들의 "0건"·"델타 0"이 결함이 아니라 그냥 아무것도
# 돌지 않은 것이다. 빈 집합을 통과로 읽지 않기 위한 선행 판정이다.
$maxPost = 0
if ($e1.Count -gt 0) { $maxPost = ($e1 | Measure-Object -Property Post -Maximum).Maximum }
"판정 0  재생 중 틱      : post 최대 $maxPost (기대 > 0)"
if ($maxPost -le 0) {
    $failed += "재생 중 관리 틱이 한 번도 돌지 않았다 — play가 먹지 않았거나 프로브가 _active에 들어가지 못했다. 아래 판정은 전부 무의미하다"
}

# ── 판정 1 (T1) — 편집 모드에서 생성 훅이 0건인가 ──
#
# 대상은 #2뿐이다(#1은 script.add의 동기 드레인에 오염됨). 종료 시 Clear가 주는
# 축소 훅은 정당하므로 세지 않는다 — 생성 축 넷만 본다.
$birthHooks = @('OnInitialized', 'OnAddedToScene', 'OnBeginSimulation', 'OnEnable')
$editModeBirths = @($e2 | Where-Object { $_.Kind -eq 'hook' -and $birthHooks -contains $_.Name })
$e2Ticks = @($e2 | Where-Object { $_.Kind -eq 'tick' })
"판정 1  편집 모드 생성훅: $($editModeBirths.Count) 건 (기대 0) · 그때 틱 $($e2Ticks.Count) 건"
if ($editModeBirths.Count -gt 0) {
    $names = ($editModeBirths | ForEach-Object { $_.Name }) -join ', '
    $failed += "편집 모드에서 생성 훅이 $($editModeBirths.Count)건 불렸다($names) — SceneManagers->Editor()의 매 프레임 DrainPendingLifecycle이 재생 여부와 무관하게 6단계를 돌린다. EditorMain.cpp:510의 규약과 어긋난다"
}

# ── 판정 2a — 축소 삼단이 존재하는가 ──
#
# 지금도 초록이다(종료 시 Clear가 준다). 2b의 전제일 뿐이라 남긴다.
foreach ($hook in @('OnEndSimulation', 'OnRemovingFromScene', 'OnUninitializing')) {
    $n = @($e1 | Where-Object { $_.Kind -eq 'hook' -and $_.Name -eq $hook }).Count
    "판정 2a $hook".PadRight(24) + ": $n 건 (기대 >= 1)"
    if ($n -lt 1) { $failed += "#1에 $hook 이 한 번도 오지 않았다" }
}

# ── 판정 2b (T2) — 축소가 **정지 시점**에 오는가 ──
#
# 개수가 아니라 시점을 본다. 옳은 계약은 "정지가 옛 인스턴스를 정리한 뒤에
# 복원분이 선다"이므로, #1의 OnUninitializing은 #2의 첫 줄보다 앞서야 한다.
$uninit1 = @($e1 | Where-Object { $_.Kind -eq 'hook' -and $_.Name -eq 'OnUninitializing' })
if ($e2.Count -eq 0) {
    "판정 2b 축소 시점      : 판정 불가 — 앵커(#2)가 없다"
    $failed += "복원분 인스턴스(#2)가 없어 축소 시점을 판정할 수 없다. 정지가 스크립트를 되살리지 않았거나 T1 고침으로 편집 모드 생성이 사라진 것이다 — 후자라면 이 판정의 앵커를 다시 잡을 것(빈 집합을 통과로 읽지 않으려고 일부러 실패시킨다)"
} elseif ($uninit1.Count -lt 1) {
    "판정 2b 축소 시점      : 판정 불가 — #1 OnUninitializing 없음"
} else {
    $uninitFrame = $uninit1[0].Frame
    $anchorFrame = $e2[0].Frame
    "판정 2b 축소 시점      : #1 Uninit f$uninitFrame vs #2 첫 줄 f$anchorFrame (기대: 앞서야 함)"
    if ($uninitFrame -ge $anchorFrame) {
        $failed += "#1의 축소가 복원분(#2)보다 늦다(f$uninitFrame >= f$anchorFrame) — 정지 시퀀스의 네이티브 축소가 DispatchLifecycle의 IsAlive 가드에 버려지고, 폴백 TearDown은 관리 틱 안의 Flush에만 있어 편집 모드에서 돌지 않는다. 지금 실제 발화 지점은 프로세스 종료의 ScriptRegistry.Clear다"
    }
}

# ── 판정 2c — 축소를 한 번도 못 받은 인스턴스가 있는가 ──
#
# 첫 실행이 물어 온 항목이다(정적 독해로는 예측하지 못했다). 편집 모드에서 만들어진
# 인스턴스는 관리 틱이 없어 Flush를 거치지 못하고 _pendingAdd에 머문다. 그런데
# 프로세스 종료의 ScriptRegistry.Clear는 _active만 훑고 _pendingAdd는 그냥 비운다
# (ScriptRegistry.cs:464 이하) — 그래서 #2는 생성 훅 넷만 받고 축소를 영영 못 받는다.
#
# 서수를 특정하지 않고 "축소를 못 받은 인스턴스 수"로 재는 이유: T1이 고쳐져
# 편집 모드 생성이 사라지면 #2 자체가 없어지는데, 그때 이 판정은 0으로 자연히
# 초록이 된다(빈 집합을 통과로 읽는 것이 아니라 실제로 대상이 없어진 것이다).
$ordinals = @($events | ForEach-Object { $_.Ordinal } | Sort-Object -Unique)
$neverTornDown = @()
foreach ($o in $ordinals) {
    $own = @($events | Where-Object { $_.Ordinal -eq $o -and $_.Kind -eq 'hook' -and $_.Name -eq 'OnUninitializing' })
    if ($own.Count -eq 0) { $neverTornDown += $o }
}
"판정 2c 축소 미수신    : $($neverTornDown.Count) 인스턴스 (기대 0) · 전체 $($ordinals.Count) 개"
if ($neverTornDown.Count -gt 0) {
    $list = ($neverTornDown | ForEach-Object { "#$_" }) -join ', '
    $failed += "$list 이(가) OnUninitializing을 한 번도 받지 못했다 — 편집 모드 생성분은 관리 틱이 없어 Flush를 못 거치고 _pendingAdd에 머무는데, 종료 시 ScriptRegistry.Clear는 _active만 훑고 _pendingAdd는 비우기만 한다. 스크립트가 잡은 것이 전부 그대로 샌다"
}

# ── 판정 3 (T3) — 비활성 전파가 스크립트에 닿는가 ──
#
# 개수는 이빨이 없다(종료 시 Clear가 OnDisable을 한 번 준다). 위치를 본다:
# OnDisable은 predisable 마크와 disabled 마크 **사이**에 있어야 한다.
$iPre  = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'predisable'
$iDis  = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'disabled'
$iPost = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'postdisable'
$iRe   = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'reenabled'
$iPostRe = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'postreenable'

if ($iPre -lt 0 -or $iDis -lt 0 -or $iPost -lt 0) {
    "판정 3  비활성 전파    : 판정 불가 — 마크가 없다 (predisable=$iPre disabled=$iDis postdisable=$iPost)"
    $failed += "비활성 왕복 마크가 로그에 없다 — OnSimulate가 Scope.Delay에서 재개되지 못했다(재생 구간 프레임 예산 부족 또는 Scope 미구동)"
} else {
    $disableBetween = @($e1[$iPre..$iDis] | Where-Object { $_.Kind -eq 'hook' -and $_.Name -eq 'OnDisable' })
    "판정 3  비활성 전파    : predisable~disabled 구간 OnDisable $($disableBetween.Count) 건 (기대 1)"
    if ($disableBetween.Count -lt 1) {
        $failed += "Entity.SetEnabled(false) 직후에 OnDisable이 오지 않았다 — ScriptComponent가 OnEnable/OnDisable을 override하지 않아 Entity::SetEnabled의 컴포넌트 전파가 C#에 닿지 않는다(Component.cpp:29 / ScriptComponent.h:29)"
    }

    # ── 판정 4 (T3) — 꺼진 스크립트의 틱이 멎는가 ──
    $deltaOff = $e1[$iPost].Post - $e1[$iDis].Post
    "판정 4  비활성 중 틱   : post 증가 $deltaOff 회 (기대 0)"
    if ($deltaOff -gt 0) {
        $failed += "오브젝트를 껐는데 PostPhysics가 $deltaOff 회 더 돌았다 — 틱 게이트가 관리 측 Component.Enabled만 보고 네이티브 활성 상태를 보지 않는다(ScriptRegistry.cs:356)"
    }

    # ── 판정 5 — 재활성이 되살리는가 ──
    #
    # 고침의 방향을 가른다. 이 값이 0이면 문제는 '못 끄는 것'이 아니라
    # '못 되살리는 것'이 되어 고칠 곳이 정반대다.
    if ($iRe -ge 0 -and $iPostRe -ge 0) {
        $deltaOn = $e1[$iPostRe].Post - $e1[$iRe].Post
        "판정 5  재활성 후 틱 : post 증가 $deltaOn 회 (기대 > 0)"
        if ($deltaOn -le 0) {
            $failed += "재활성 뒤에도 틱이 돌지 않는다 — 되살리는 경로가 없다"
        }
    } else {
        "판정 5  재활성 후 틱 : 판정 불가 — 마크가 없다"
        $failed += "재활성 마크가 없다 — OnSimulate가 끝까지 진행하지 못했다(재생 구간 프레임 예산 확인)"
    }
}

# ── 판정 6 — 역방향(C# → 네이티브)이 실제로 그 다리를 건너는가 ──
#
# 위 판정 3·4는 Entity.SetEnabled(네이티브가 시작점)만 태운다. Component.Enabled의
# setter는 반대 방향이라 따로 태워야 한다 — 배선만 하고 소비자가 0이면 그 코드는
# 죽은 채로 초록이다.
#
# ★ 훅과 틱만 보면 이빨이 없다. setter는 전달에 실패해도 국소 폴백으로 내려가
#   훅을 부르고 틱도 멎게 하므로, 다리가 죽어도 판정이 똑같이 보인다. 그래서
#   폴백 경고 0건을 함께 판정한다 — 그것만이 "경계를 실제로 건넜다"를 말한다.
$iSelfDis     = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'selfdisabled'
$iPostSelfDis = Get-MarkIndex -List $e1 -Kind 'mark' -Name 'postselfdisable'
$fallbackWarn = ([regex]::Matches($logText, '를 네이티브에 전달하지 못했다')).Count

if ($iSelfDis -lt 0 -or $iPostSelfDis -lt 0) {
    "판정 6  역방향 전파    : 판정 불가 — 마크가 없다 (selfdisabled=$iSelfDis postselfdisable=$iPostSelfDis)"
    $failed += "역방향 왕복 마크가 없다 — OnSimulate가 끝까지 진행하지 못했다(재생 구간 프레임 예산 확인)"
} else {
    $selfDisableHooks = @($e1[$iPre..$iSelfDis] | Where-Object { $_.Kind -eq 'hook' -and $_.Name -eq 'OnDisable' })
    $deltaSelfOff = $e1[$iPostSelfDis].Post - $e1[$iSelfDis].Post
    "판정 6  역방향 전파    : 비활성 중 틱 $deltaSelfOff 회 (기대 0) · 폴백 경고 $fallbackWarn 건 (기대 0)"
    if ($deltaSelfOff -gt 0) {
        $failed += "Component.Enabled=false 뒤에도 PostPhysics가 $deltaSelfOff 회 더 돌았다"
    }
    if ($fallbackWarn -gt 0) {
        $failed += "Component.Enabled의 국소 폴백 경고가 $fallbackWarn 건 있다 — setter가 Api_Script_SetEnabled로 네이티브에 닿지 못했다(그 다리가 죽어 있어도 훅과 틱은 폴백 덕에 정상으로 보인다)"
    }
}

# ── 판정 7 — 축소 훅에서 자기 오브젝트에 닿는가 ──
#
# 훅이 왔다는 것만으로는 반쪽이다. Entity::Destroy가 파괴 **표시** 시점에 스크립트
# 핸들을 무효화하면, 축소가 발화하는 FlushPendingDestroy 무렵에는 이미 닿을 수
# 없다 — 이름은 빈 문자열이고 GetComponent는 전부 무응답이다. 크래시가 아니라
# '정리 코드가 조용히 아무 일도 안 하는' 모습이라 개수 판정으로는 안 보인다.
$reachable = @($events | Where-Object { $_.Kind -eq 'mark' -and $_.Name -eq 'ownerreachable' })
$lost      = @($events | Where-Object { $_.Kind -eq 'mark' -and $_.Name -eq 'ownerlost' })
"판정 7  축소 중 소유자  : 도달 $($reachable.Count) 건 · 유실 $($lost.Count) 건 (기대: 유실 0, 도달 >= 1)"
if ($lost.Count -gt 0) {
    $failed += "축소 훅에서 자기 오브젝트에 닿지 못한 인스턴스가 $($lost.Count)건이다 — 파괴 표시 시점에 스크립트 핸들이 무효화되어 마지막 훅이 빈손으로 온다(Entity::Destroy의 ScriptObjectRegistry::Unregister 위치 확인)"
}
if ($reachable.Count -lt 1) {
    $failed += "축소 훅에서 소유자 도달을 확인한 인스턴스가 0건이다 — OnUninitializing 자체가 오지 않았거나 프로브가 그 마크를 남기지 못했다"
}

# ── 판정 8 — 축소가 경로와 무관하게 비활성으로 시작하는가 ──
#
# Unity가 파괴에서 OnDisable → OnDestroy를 보장하는 것과 같은 계약이다.
# 어셈블리 리로드 경로(ScriptRegistry.Clear)는 예전부터 이것을 했지만 재생 정지의
# 파괴 경로는 하지 않았다 — 같은 최종 정리가 경로마다 달랐다. 인스턴스마다
# OnEndSimulation 바로 앞에 OnDisable이 있는지 본다.
#
# 프로브는 항상 활성 상태로 파괴되므로 "있어야 한다"가 옳다. 이미 꺼진
# 스크립트라면 전이가 아니라 발화하지 않는 것이 맞다(그것도 Unity와 같다).
$teardownOrderBad = @()
foreach ($o in $ordinals) {
    $own = @($events | Where-Object { $_.Ordinal -eq $o })
    $iEnd = Get-MarkIndex -List $own -Kind 'hook' -Name 'OnEndSimulation'
    if ($iEnd -lt 0) { continue }
    $before = @($own[0..$iEnd] | Where-Object { $_.Kind -eq 'hook' -and $_.Name -eq 'OnDisable' })
    # 축소 직전의 OnDisable인지 확인한다 — 앞선 비활성 왕복의 것과 섞이면 안 되므로
    # 마지막 OnDisable이 OnEndSimulation 직전 2줄 안에 있어야 한다.
    if ($before.Count -eq 0) { $teardownOrderBad += $o; continue }
    $iLastDisable = -1
    for ($i = $iEnd; $i -ge 0; --$i) {
        if ($own[$i].Kind -eq 'hook' -and $own[$i].Name -eq 'OnDisable') { $iLastDisable = $i; break }
    }
    if ($iEnd - $iLastDisable -gt 2) { $teardownOrderBad += $o }
}
"판정 8  축소 시작 비활성: 어긋난 인스턴스 $($teardownOrderBad.Count) 개 (기대 0) · 전체 $($ordinals.Count) 개"
if ($teardownOrderBad.Count -gt 0) {
    $list = ($teardownOrderBad | ForEach-Object { "#$_" }) -join ', '
    $failed += "$list 의 축소가 OnDisable 없이 시작한다 — 파괴 경로와 어셈블리 리로드 경로(Clear)가 같은 최종 정리를 다르게 한다. OnEnable/OnDisable에 구독을 건 스크립트가 파괴에서 해지되지 않는다"
}

if ($proc.ExitCode -ne 0) { $failed += ("종료 코드 비정상: 0x{0:X8}" -f $proc.ExitCode) }

""
if ($failed.Count -gt 0) {
    "실패 $($failed.Count)건:"
    $failed | ForEach-Object { "  · $_" }
    ""
    "※ 고침 전에는 붉은 것이 정상이다. 초록이 되면 run-all.ps1에 넣을 것."
    exit 1
}

"전체 통과 — 편집 모드는 훅을 돌리지 않고, 정지가 축소를 제때 전달하며, 비활성이 스크립트까지 닿는다"
exit 0
