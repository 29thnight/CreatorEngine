using System.Text;

namespace CreatorBuildTool;

// PrSM 스크립트의 Player AOT 모듈(D-048 동적 AOT 패키징).
//
// Player 에는 LLVM 도, 게임마다 다시 링크할 엔진 소스도 없다. 그래서 패키징이 프로젝트의
// PrSM 소스를 엔진 배포본의 prsmc 로 오브젝트로 만들고, lld-link 로 DLL 하나에 링크한다.
// 그 DLL 은 C 런타임 없이(/NODEFAULTLIB /NOENTRY) Player.runtime.dll 의 export 만 import 한다:
// ce_* 런타임, prsm_host_* 태스크·리로드 브리지, ce_prsm_validate_binding, reflgen 이 생성한
// 엔진 바인딩 래퍼. import 라이브러리는 배포본 Player.runtime.dll 의 실제 export 표에서 만든다 —
// 스크립트가 그 Player 에 없는 심볼을 부르면 링크가 여기서 실패한다(실행 중이 아니라).
// 카탈로그는 배포본에 배치된 *.bindings.json 전부다. 바인딩 서명은 실행 때도 호출마다
// 런타임 DLL 의 컴파일된 표로 다시 검증된다.
internal static class PrsmModuleBuilder
{
    public const string RelativeModulePath = "PrSM/GameScripts.prsm.dll";
    public const string ProjectManifest = "Assets/Scripts/PrSM/project.json";

    // PrSM 프로젝트가 없으면 null — PrSM 을 쓰지 않는 프로젝트의 정상 경로다.
    public static async Task<string?> Build(BuildContext context, EngineDistribution engine, string project,
        string candidate, string work, string configuration)
    {
        var manifest = Path.Combine(project, ProjectManifest);
        if (!File.Exists(manifest)) return null;
        var tools = Paths.Child(engine.BinaryRoot, "PrSM");
        string Tool(string name)
        {
            var path = Path.Combine(tools, name);
            if (!File.Exists(path))
                throw new BuildException($"The engine distribution has no PrSM toolchain file {name}. Rebuild SceneRuntime and publish the engine.");
            return path;
        }
        var compiler = Tool("prsmc.exe"); var linker = Tool("lld-link.exe"); _ = Tool("LLVM-C.dll");
        var catalogs = Paths.Files(tools).Where(p => p.EndsWith(".bindings.json", StringComparison.OrdinalIgnoreCase))
            .Order(StringComparer.Ordinal).ToArray();
        if (catalogs.Length == 0) throw new BuildException("The engine distribution has no PrSM binding catalog.");
        var runtime = Paths.Child(engine.BinaryRoot, "Player/Player.runtime.dll");
        Directory.CreateDirectory(work);

        var objectPath = Path.Combine(work, "GameScripts.obj");
        var support = Path.Combine(work, "GameScripts.support.obj");
        var arguments = new List<string> { "build-module", "--project", manifest, "--no-cache", "--color", "never", "-o", objectPath };
        if (configuration == "Release") arguments.AddRange(["--opt-level", "2"]);
        foreach (var catalog in catalogs) { arguments.Add("--bindings"); arguments.Add(catalog); }
        await context.Run(compiler, arguments, tools);
        await context.Run(compiler, ["emit-dll-support", "-o", support], tools);

        var imports = PeExports.Read(runtime).Where(Importable).Order(StringComparer.Ordinal).ToArray();
        if (!imports.Contains("ce_prsm_validate_binding"))
            throw new BuildException("Player.runtime.dll does not export the PrSM binding validator. Rebuild and publish the engine.");
        var definition = Path.Combine(work, "Player.runtime.def");
        File.WriteAllLines(definition, ["LIBRARY Player.runtime.dll", "EXPORTS", .. imports]);
        var importLibrary = Path.Combine(work, "Player.runtime.lib");
        await context.Run(linker, ["/lib", "/nologo", "/machine:x64", "/def:" + definition, "/out:" + importLibrary], tools);

        var module = Paths.Child(candidate, RelativeModulePath);
        Directory.CreateDirectory(Path.GetDirectoryName(module)!);
        await context.Run(linker, ["/nologo", "/dll", "/noentry", "/nodefaultlib", "/noimplib", "/machine:x64", "/opt:ref",
            "/incremental:no", "/brepro", "/export:prsm_module_get", "/out:" + module, objectPath, support, importLibrary], tools);
        if (!PeExports.Read(module).Contains("prsm_module_get"))
            throw new BuildException("The linked PrSM module does not export prsm_module_get.");
        context.Log($"PrSM AOT module {RelativeModulePath}: {catalogs.Length} catalog(s), {imports.Length} runtime imports available");
        return RelativeModulePath;
    }

    // 스크립트 모듈이 부를 수 있는 런타임 export. 그 밖의 export 는 import 라이브러리에 넣지 않는다.
    private static bool Importable(string name) => name.StartsWith("ce_", StringComparison.Ordinal) || name.StartsWith("prsm_host_", StringComparison.Ordinal);
}

// PE32+ export 이름 표 읽기 — import 라이브러리를 실제 바이너리에서 만들기 위해서만 쓴다.
internal static class PeExports
{
    public static string[] Read(string path)
    {
        var image = File.ReadAllBytes(path);
        int Int32(long offset) => offset >= 0 && offset + 4 <= image.Length ? BitConverter.ToInt32(image, (int)offset) : throw Invalid();
        int UInt16(long offset) => offset >= 0 && offset + 2 <= image.Length ? BitConverter.ToUInt16(image, (int)offset) : throw Invalid();
        BuildException Invalid() => new($"Not a valid PE32+ image: {path}");
        if (UInt16(0) != 0x5a4d) throw Invalid();
        var pe = Int32(0x3c);
        if (Int32(pe) != 0x00004550) throw Invalid();
        var sections = UInt16(pe + 6); var optionalSize = UInt16(pe + 20); var optional = pe + 24;
        if (UInt16(optional) != 0x20b) throw Invalid();
        var exportRva = Int32(optional + 112); // DataDirectory[0] of a PE32+ optional header
        if (exportRva == 0) return [];
        var table = optional + optionalSize;
        long Offset(int rva)
        {
            for (var i = 0; i < sections; ++i)
            {
                var header = table + i * 40;
                var virtualAddress = Int32(header + 12); var size = Math.Max(Int32(header + 8), Int32(header + 16));
                if (rva >= virtualAddress && rva < virtualAddress + size) return (long)rva - virtualAddress + Int32(header + 20);
            }
            throw Invalid();
        }
        var directory = Offset(exportRva);
        var count = Int32(directory + 24); var names = Offset(Int32(directory + 32));
        if (count is < 0 or > 1_000_000) throw Invalid();
        var result = new string[count];
        for (var i = 0; i < count; ++i)
        {
            var start = Offset(Int32(names + 4L * i)); var end = start;
            while (end < image.Length && image[end] != 0) ++end;
            if (end >= image.Length) throw Invalid();
            result[i] = Encoding.ASCII.GetString(image, (int)start, (int)(end - start));
        }
        return result;
    }
}
