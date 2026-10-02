$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture=Join-Path $repo 'Build/Obj/Phase19L0/Release/Layers.celayers'
if(!(Test-Path -LiteralPath $fixture)){throw 'Run verify-layer-catalog first to generate the native CLYR fixture'}
$out=Join-Path $repo 'Build/Obj/Phase19L0Package'
New-Item -ItemType Directory -Force $out|Out-Null
$source=[Security.SecurityElement]::Escape((Join-Path $repo 'BuildTool/ProjectLayerAsset.cs'))
@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup>
  <ItemGroup><Compile Include="$source" Link="ProjectLayerAsset.cs" /></ItemGroup>
</Project>
"@ | Set-Content (Join-Path $out 'Probe.csproj') -Encoding utf8
@'
namespace CreatorBuildTool;
internal static class Paths { public static string Child(string root, string path) => Path.Combine(root, path); }
internal sealed class BuildException(string message) : Exception(message);
internal static class Program
{
    static int checks;
    static void Expect(Action action, bool pass)
    {
        ++checks;
        try { action(); if(!pass) throw new Exception("Invalid asset accepted"); }
        catch(BuildException) { if(pass) throw; }
    }
    static void Main(string[] args)
    {
        var root=Path.Combine(args[1], "fixture");
        Directory.CreateDirectory(Path.Combine(root, "ProjectSetting"));
        var target=Path.Combine(root, ProjectLayerAsset.RelativePath);
        var original=File.ReadAllBytes(args[0]);
        File.WriteAllBytes(target, original);
        Expect(()=>ProjectLayerAsset.Require(root), true);
        var corrupt=(byte[])original.Clone(); corrupt[40]^=1;
        File.WriteAllBytes(target, corrupt);
        Expect(()=>ProjectLayerAsset.Require(root), false);
        corrupt=(byte[])original.Clone(); corrupt[4]=2;
        File.WriteAllBytes(target, corrupt);
        Expect(()=>ProjectLayerAsset.Require(root), false);
        File.WriteAllBytes(target, new byte[65537]);
        Expect(()=>ProjectLayerAsset.Require(root), false);
        File.Delete(target);
        Expect(()=>ProjectLayerAsset.Require(root), false);
        File.WriteAllBytes(target, original);
        Expect(()=>ProjectLayerAsset.Require(root), true);
        Console.WriteLine($"PROJECT_LAYER_PACKAGE_OK checks={checks}");
    }
}
'@ | Set-Content (Join-Path $out 'Program.cs') -Encoding utf8
& dotnet run --project (Join-Path $out 'Probe.csproj') --configuration Release -- $fixture $out *> (Join-Path $out 'result.log')
if($LASTEXITCODE){Get-Content (Join-Path $out 'result.log') -Tail 15;throw 'Project layer package gate failed'}
Get-Content (Join-Path $out 'result.log')
