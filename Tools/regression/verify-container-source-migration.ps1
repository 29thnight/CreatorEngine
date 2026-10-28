$ErrorActionPreference='Stop'
. "$PSScriptRoot/../build/CppContainerTokens.ps1"
$source=@'
// std::vector<int> remains a historical comment
/* std::vector<long> */
const char* legacy="std::vector<int>";
const char* raw=R"tag(std::vector<float> and "quotes")tag";
auto digits=1'000'000;
auto character='v';
std::vector<std::vector<int>> values;
std::erase_if(values, predicate);
'@
$after=Convert-CppVectorTokens $source
if($after -notmatch 'ce::dynamic_array<ce::dynamic_array<int>> values;'){throw 'Nested vector migration failed'}
if($after -notmatch 'ce::erase_if\(values, predicate\)'){throw 'Erase adaptation failed'}
foreach($protected in @('// std::vector<int> remains a historical comment','/* std::vector<long> */','"std::vector<int>"','R"tag(std::vector<float> and "quotes")tag"',"1'000'000")){
 if(!$after.Contains($protected)){throw "Changed protected source: $protected"}
}
$references=@(Get-CppContainerReferences $source)
if($references.Count -ne 2){throw "Expected two code references, got $($references.Count)"}
$work=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Build/Verification/ContainerMigration/lexer'))
New-Item -ItemType Directory -Force $work|Out-Null
$mixed=Join-Path $work 'mixed-comment.cpp'
$bytes=[Text.Encoding]::Latin1.GetBytes("// $([char]0x81)`nstd::vector<int> values;`n")
[IO.File]::WriteAllBytes($mixed,$bytes)
$decoded=Read-CppMigrationSource $mixed
$converted=Convert-CppVectorTokens $decoded.Text
$output=$decoded.Encoding.GetBytes($converted)
if($output[3] -ne 129 -or $converted -notmatch 'ce::dynamic_array<int>'){throw 'Mixed comment bytes were changed'}
'CONTAINER_SOURCE_MIGRATION_LEXER_PASS'
