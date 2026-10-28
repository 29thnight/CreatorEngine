# Token protection for source migration. Keep literals, comments and serialized
# type-name strings intact; this is not a C++ semantic parser.
$script:CppContainerProtected='(?<protected>(?:u8|u|U|L)?R"(?<rawtag>[^\s()\\]{0,16})\((?s:.*?)\)\k<rawtag>"|(?:u8|u|U|L)?"(?:\\.|[^"\\\r\n])*"|(?<![A-Za-z0-9_])(?:u8|u|U|L)?''(?:\\.|[^''\\\r\n])*''|//[^\r\n]*|/\*(?s:.*?)\*/)'
$script:CppContainerKinds='vector|deque|list|forward_list|map|multimap|unordered_map|unordered_multimap|set|multiset|unordered_set|unordered_multiset|array|queue|priority_queue|stack|basic_string|string|wstring|u8string|u16string|u32string|function|bitset'
function Get-CppContainerReferences([string]$Text){
 $pattern=$script:CppContainerProtected+'|(?<reference>\bstd::(?:(?<pmr>pmr)::)?(?<kind>'+$script:CppContainerKinds+')\b)'
 foreach($match in [regex]::Matches($Text,$pattern)){
  if($match.Groups['reference'].Success){$match}
 }
}
function Convert-CppVectorTokens([string]$Text){
 $pattern=$script:CppContainerProtected+'|(?<vector>\bstd::vector\b)|(?<erase>\bstd::erase(?:_if)?)(?=\s*\()'
 $callback=[Text.RegularExpressions.MatchEvaluator]{param($match)
  if($match.Groups['protected'].Success){return $match.Value}
  if($match.Groups['vector'].Success){return 'ce::dynamic_array'}
  return $match.Value.Replace('std::','ce::')
 }
 [regex]::Replace($Text,$pattern,$callback)
}
function Read-CppMigrationSource([string]$Path){
 $raw=[IO.File]::ReadAllBytes($Path)
 $bom=$raw.Length -ge 3 -and $raw[0] -eq 239 -and $raw[1] -eq 187 -and $raw[2] -eq 191
 $encoding=[Text.UTF8Encoding]::new($false,$true)
 if($raw.Length -ge 2 -and $raw[0] -eq 255 -and $raw[1] -eq 254){
  $encoding=[Text.UnicodeEncoding]::new($false,$true,$true);$text=$encoding.GetString($raw)
 }elseif($raw.Length -ge 2 -and $raw[0] -eq 254 -and $raw[1] -eq 255){
  $encoding=[Text.UnicodeEncoding]::new($true,$true,$true);$text=$encoding.GetString($raw)
 }else{
  try{$text=$encoding.GetString($raw);$encoding=[Text.UTF8Encoding]::new($bom,$true)}catch{
   [Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
   $encoding=[Text.Encoding]::GetEncoding(949,[Text.EncoderExceptionFallback]::new(),[Text.DecoderExceptionFallback]::new())
   try{$text=$encoding.GetString($raw)}catch{
    # Some legacy files mix UTF8 edits with older comments. A byte-preserving
    # fallback is safe only when every non-ASCII byte is inside a comment.
    $encoding=[Text.Encoding]::Latin1;$text=$encoding.GetString($raw)
    $comments=@([regex]::Matches($text,$script:CppContainerProtected)|Where-Object {$_.Value.StartsWith('//') -or $_.Value.StartsWith('/*')})
    $index=0
    for($i=0;$i -lt $raw.Length;++$i){if($raw[$i] -ge 128){
      while($index -lt $comments.Count -and $comments[$index].Index+$comments[$index].Length -le $i){++$index}
      if($index -ge $comments.Count -or $i -lt $comments[$index].Index){throw "Non-ASCII code/literal requires encoding review: $Path"}
    }}
   }
   if([Convert]::ToHexString($encoding.GetBytes($text)) -ne [Convert]::ToHexString($raw)){throw "Encoding roundtrip failed: $Path"}
  }
 }
 if($text.Length -and $text[0] -eq [char]0xfeff){$text=$text.Substring(1)}
 [pscustomobject]@{Text=$text;Bom=$bom;Encoding=$encoding}
}
