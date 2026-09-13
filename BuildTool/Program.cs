namespace CreatorBuildTool;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        using var cancellation = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancellation.Cancel(); };
        BuildContext? context = null;
        try
        {
            var options = new Options(args);
            context = new BuildContext(options, cancellation.Token);
            switch (options.Command)
            {
                case "help":
                    context.Log("CreatorBuildTool: publish-engine | compile-game | package-game | verify-engine | select-engine | open-project\n" +
                        "  publish-engine --repository PATH --config Debug|Release [--output-root PATH] [--build] [--shipping] [--no-pointer]\n" +
                        "  compile-game --engine-distribution PATH --project PATH --output PATH [--config Release] [--prebuilt-assembly FILE]\n" +
                        "  package-game --engine-distribution PATH --project PATH [--config Debug] [--stage-root PATH]\n" +
                        "    [--input-mode Project|Workspace|Tracked] [--startup-scene NAME.creator] [--render-backend dx12|vulkan]\n" +
                        "    [--shipping] [--build-native] [--skip-verify] [--smoke-frames 120] [--smoke-timeout-sec 180]\n" +
                        "  verify-engine | select-engine --engine-distribution PATH [--project PATH]\n" +
                        "  open-project --engine-distribution PATH --development-project PATH [--select-engine]\n" +
                        "  Common: --log-path FILE --json (JSON Lines), Ctrl+C cancels the process tree.\n" +
                        "  --skip-verify retains an unpublished candidate; it never changes the current package.");
                    break;
                case "publish-engine": await EnginePublisher.Publish(context); break;
                case "compile-game":
                {
                    var engine = EngineDistribution.Load(options.Required("engine-distribution"), context);
                    await GameCompiler.Compile(context, engine, options.Required("project"), options.Required("output"),
                        options.Choice("config", "Release", "Debug", "Release"), options.Get("prebuilt-assembly"));
                    break;
                }
                case "package-game": await GamePackager.Build(context); break;
                case "verify-engine":
                {
                    var engine = EngineDistribution.Load(options.Required("engine-distribution"), context);
                    context.Result(engine.Root); break;
                }
                case "select-engine":
                {
                    var engine = EngineDistribution.Load(options.Required("engine-distribution"), context);
                    engine.SelectProject(options.Required("project")); context.Result(engine.Root); break;
                }
                case "open-project":
                {
                    var engine = EngineDistribution.Load(options.Required("engine-distribution"), context);
                    var project = Paths.Canonical(options.Required("development-project"), true);
                    if (options.Flag("select-engine")) engine.SelectProject(project);
                    engine.AssertProject(project);
                    var managed = Paths.Child(project, $"Intermediate/Managed/{engine.Configuration}");
                    await GameCompiler.Compile(context, engine, project, managed, engine.Configuration, "");
                    await context.Run(Path.Combine(engine.BinaryRoot, "Editor/CreatorEditor.exe"),
                        ["--development-project", project, "--managed-root", managed], engine.Root, timeoutSeconds: 0);
                    break;
                }
                default: throw new BuildException($"Unknown command: {options.Command}. Use --help.");
            }
            return 0;
        }
        catch (OperationCanceledException) { context?.Error("Build cancelled; published outputs were preserved."); return 130; }
        catch (Exception ex) { if (context != null) context.Error(ex.Message); else Console.Error.WriteLine(ex.Message); return 1; }
        finally { context?.Dispose(); }
    }
}

internal sealed class BuildException(string message) : Exception(message);

internal sealed class Options
{
    private readonly Dictionary<string, string> values = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> Switches = new(["build", "shipping", "buildnative", "skipverify", "selectengine", "json", "nopointer"], StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> Names = new(["repository", "config", "outputroot", "output", "project", "developmentproject",
        "enginedistribution", "prebuiltassembly", "gamescriptsassembly", "stageroot", "inputmode", "startupscene", "renderbackend",
        "smokeframes", "smoketimeoutsec", "logpath", "target"], StringComparer.OrdinalIgnoreCase);
    public string Command { get; }
    private static string Key(string name) => name.TrimStart('-').Replace("-", "");
    public Options(string[] args)
    {
        Command = args.Length == 0 || args[0] is "--help" or "-h" ? "help" : args[0].ToLowerInvariant();
        for (var i = 1; i < args.Length; ++i)
        {
            if (!args[i].StartsWith('-')) throw new BuildException($"Expected an option, got {args[i]}");
            var name = Key(args[i]);
            if (!Names.Contains(name) && !Switches.Contains(name)) throw new BuildException($"Unknown option: {args[i]}");
            var value = Switches.Contains(name) ? "true" : ++i < args.Length ? args[i] : throw new BuildException($"Missing value for {name}");
            if (!values.TryAdd(name, value)) throw new BuildException($"Duplicate option: {name}");
        }
    }
    public string Get(string name, string fallback = "") => values.GetValueOrDefault(Key(name), fallback);
    public string Required(string name) => Get(name) is { Length: > 0 } value ? value : throw new BuildException($"--{name} is required.");
    public bool Flag(string name) => Get(name) == "true";
    public string Choice(string name, string fallback, params string[] allowed) =>
        allowed.FirstOrDefault(v => v.Equals(Get(name, fallback), StringComparison.OrdinalIgnoreCase)) ?? throw new BuildException($"Invalid --{name}: {Get(name)}");
    public int Number(string name, int fallback, int min, int max) => int.TryParse(Get(name, fallback.ToString()), out var value) && value >= min && value <= max
        ? value : throw new BuildException($"--{name} must be in {min}..{max}.");
}
