// Included in the asset-command namespace. This probe never generates edits:
// actual Inspector input must enqueue the save before the race is requested.
static CommandCore::CommandResult Cmd_texture_auto_ui_pending_probe(const ConsoleCommandContext& ctx)
{
    using namespace CommandCore;
    if (ctx.parts.size() != 3u || (ctx.parts[2] != "close" && ctx.parts[2] != "shutdown" &&
        ctx.parts[2] != "conflict" && ctx.parts[2] != "conflictshutdown"))
    {
        return InvalidArguments("assets.texture.pendingprobe <isolated-source> <close|shutdown|conflict|conflictshutdown>");
    }
    const auto source = file::path(ctx.parts[1]);
    const auto project = source.parent_path().parent_path().parent_path();
    if (source.filename() != "fixture.png" || !file::exists(project / ".texture-auto-ui-race"))
    {
        return PreconditionFailed("texture.pending.isolation", "Use the marked isolated UI regression project");
    }
    const auto sidecar = file::path(source.string() + ".meta");
    const auto read = [sidecar]
    {
        std::ifstream stream(sidecar, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    const auto before = read();
    if (before.empty() || EditorAssetDatabase::Get().PendingAutomaticSaveAge(sidecar))
    {
        return PreconditionFailed("texture.pending.baseline", "Use a readable sidecar without an existing pending save");
    }
    const auto mode = ctx.parts[2];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    auto* system = &ctx.system;
    ctx.system.WaitForResult([read, before, sidecar, mode, deadline, system, requested = false,
        external = std::string{}, settled = std::chrono::steady_clock::time_point{},
        requestedAge = std::int64_t{}]() mutable -> std::optional<CommandResult>
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return Fail("texture.pending.timeout", "No Inspector pending-save race completed within five minutes");
        }
        auto& database = EditorAssetDatabase::Get();
        const auto age = database.PendingAutomaticSaveAge(sidecar);
        if (!requested)
        {
            if (!age)
            {
                if (read() != before)
                {
                    return Fail("texture.pending.missed", "Disk changed before a pending UI save was observed");
                }
                return std::nullopt;
            }
            if (age->count() >= 400 || read() != before)
            {
                return Fail("texture.pending.late", "Race request did not precede the 400ms save deadline");
            }
            requestedAge = age->count();
            requested = true;
            std::printf("[TEXTURE_PENDING] mode=%s requestAgeMs=%lld disk=unchanged\n",
                mode.c_str(), static_cast<long long>(requestedAge));
            if (mode == "conflict" || mode == "conflictshutdown")
            {
                std::string failure;
                auto document = Authoring::WriteDocument::ParseFile(sidecar, &failure);
                experiment::cooked::TextureImportSettings settings;
                if (!document || !experiment::cooked::ParseTextureImportSettings(before, settings, failure))
                {
                    return Fail("texture.pending.external", "Could not prepare external settings");
                }
                settings.maxDimension = 1u;
                EditorAssetDatabase::WriteTextureImportSettings(document->Root(), settings);
                external = document->Dump();
                std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
                output << external;
                output.close();
                if (!output || read() != external)
                {
                    return Fail("texture.pending.external", "External write failed");
                }
                std::printf("[TEXTURE_EXTERNAL_CONFLICT] mode=%s externalMaximumDimension=1\n", mode.c_str());
                if (mode == "conflictshutdown")
                {
                    system->RequestQuit();
                    auto data = CommandData::Object();
                    data.Set("requestAgeMs", CommandData::Int(requestedAge));
                    data.Set("diskUnchangedAtRequest", CommandData::Bool(true));
                    data.Set("externalMaximumDimension", CommandData::Int(1));
                    return Ok("TEXTURE_CONFLICT_SHUTDOWN_REQUESTED", std::move(data));
                }
                return std::nullopt;
            }
            if (mode == "shutdown")
            {
                system->RequestQuit();
                auto data = CommandData::Object();
                data.Set("requestAgeMs", CommandData::Int(requestedAge));
                data.Set("diskUnchangedAtRequest", CommandData::Bool(true));
                return Ok("TEXTURE_PENDING_SHUTDOWN_REQUESTED", std::move(data));
            }
            if (!::editor::queue_window_request("###Editor.Inspector", ::editor::window_request::close))
            {
                return Fail("texture.pending.panel", "Inspector close request was rejected");
            }
            return std::nullopt;
        }
        if (age || read() == before)
        {
            return std::nullopt;
        }
        if (mode == "conflict")
        {
            if (read() != external)
            {
                return Fail("texture.pending.overwrite", "Pending UI edit overwrote external settings");
            }
            if (settled == std::chrono::steady_clock::time_point{})
            {
                settled = std::chrono::steady_clock::now();
            }
            if (std::chrono::steady_clock::now() - settled < std::chrono::milliseconds(750))
            {
                return std::nullopt;
            }
            auto data = CommandData::Object();
            data.Set("requestAgeMs", CommandData::Int(requestedAge));
            data.Set("diskUnchangedAtRequest", CommandData::Bool(true));
            data.Set("externalBytesPreserved", CommandData::Bool(true));
            data.Set("externalMaximumDimension", CommandData::Int(1));
            return Ok("TEXTURE_PENDING_CONFLICT_PRESERVED", std::move(data));
        }
        experiment::cooked::TextureImportSettings settings;
        std::string error;
        if (!experiment::cooked::ParseTextureImportSettings(read(), settings, error) || settings.maxDimension != 2u)
        {
            return Fail("texture.pending.saved", "Expected the actual Inspector edit to persist maximum dimension 2");
        }
        auto data = CommandData::Object();
        data.Set("requestAgeMs", CommandData::Int(requestedAge));
        data.Set("diskUnchangedAtRequest", CommandData::Bool(true));
        data.Set("maximumDimension", CommandData::Int(settings.maxDimension));
        return Ok("TEXTURE_PENDING_CLOSE_SAVED", std::move(data));
    }, true);
    return Ok("Texture pending-save observer armed; edit Maximum Dimension to 2 in the Inspector");
}
