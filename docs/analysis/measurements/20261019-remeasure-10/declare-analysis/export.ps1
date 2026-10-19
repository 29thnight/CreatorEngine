$ErrorActionPreference='Stop'
$repo=$pwd.Path
$out="$repo/Artifacts/remeasure-10-20261019/after"
$dest=$PSScriptRoot
$env:_NT_SYMBOL_PATH="$out/BinarySnapshot;srv*$repo/Artifacts/etw-cpu-20261017/Symbols"
$env:_NT_SYMCACHE_PATH="$repo/Artifacts/etw-cpu-20261017/SymCache"
$ranges=Get-Content "$out/ranges.json" -Raw|ConvertFrom-Json
foreach($mode in @('on','off')) {
 $range=$ranges.$mode
 & 'C:/Program Files (x86)/Windows Kits/10/Windows Performance Toolkit/xperf.exe' -i "$out/creator-robot.etl" -symbols -o "$dest/declare-$mode.html" -a stack -pid 36772 -tid 26056 -event 'Sampled Profile' -symbol 'LivePipelineDesc::DeclareAll' -butterfly 1 -range $range.beginUs $range.endUs *> "$dest/export-$mode.log"
 if($LASTEXITCODE){throw "Stack export failed $mode"}
 & python "$repo/Artifacts/etw-cpu-20261017/parse-stack.py" "$dest/declare-$mode.html" *> "$dest/parse-$mode.log"
 if($LASTEXITCODE){throw "Stack parse failed $mode"}
}
