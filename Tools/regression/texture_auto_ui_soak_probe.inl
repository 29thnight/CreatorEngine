// First edit is real Inspector input; subsequent cycles exercise the same queue
// through the asset-service API inside the live Editor frame loop.
static CommandCore::CommandResult StartTextureAutomaticSaveSoak(const ConsoleCommandContext& ctx,
    const std::filesystem::path& source, const std::filesystem::path& sidecar, const std::string& before)
{
    using namespace CommandCore;
    const auto read = [](const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    const auto scene = source.parent_path().parent_path() / "Scenes" / "SavedFixture.creator";
    const auto sceneBefore = read(scene);
    const auto sourceBefore = read(source);
    const auto armed = std::chrono::steady_clock::now();
    ctx.system.WaitForResult([source, sidecar, before, read, scene, sceneBefore, sourceBefore, armed,
        phase = -2, cycles = 0u, normal = 0u, conflicts = 0u, deleted = 0u, corrupt = 0u,
        forced = 0u, firstAge = std::int64_t{}, baseline = std::string{}, external = std::string{},
        started = std::chrono::steady_clock::time_point{}, step = armed]() mutable -> std::optional<CommandResult>
    {
        const auto now = std::chrono::steady_clock::now();
        auto& database = EditorAssetDatabase::Get();
        const auto age = database.PendingAutomaticSaveAge(sidecar);
        if (now - armed > std::chrono::minutes(15) || (phase >= -1 && now - step > std::chrono::seconds(30)))
        {
            return Fail("texture.soak.timeout", "Automatic-save soak stopped making progress");
        }
        if (phase == -2)
        {
            if (!age)
            {
                if (read(sidecar) != before)
                {
                    return Fail("texture.soak.missed", "Disk changed before the actual Inspector pending save");
                }
                return std::nullopt;
            }
            if (age->count() >= 400 || read(sidecar) != before)
            {
                return Fail("texture.soak.late", "Initial Inspector save was not observed before debounce");
            }
            firstAge = age->count();
            phase = -1;
            step = now;
            return std::nullopt;
        }
        if (phase == -1)
        {
            if (age || !database.TextureImportReady(source))
            {
                return std::nullopt;
            }
            experiment::cooked::TextureImportSettings settings;
            std::string failure;
            baseline = read(sidecar);
            if (!database.AutomaticSaveError(sidecar).empty() ||
                !experiment::cooked::ParseTextureImportSettings(baseline, settings, failure) || settings.maxDimension != 2u)
            {
                return Fail("texture.soak.initial", "Actual Inspector edit did not save maximum dimension 2");
            }
            started = now;
            phase = 0;
            step = now;
        }
        const auto kind = cycles % 4u;
        if (phase == 0)
        {
            if (!database.TextureImportReady(source))
            {
                return std::nullopt;
            }
            if (read(source) != sourceBefore || read(scene) != sceneBefore || read(sidecar) != baseline)
            {
                return Fail("texture.soak.drift", "Source, saved scene, or restored metadata changed");
            }
            if (now - started >= std::chrono::seconds(300))
            {
                auto data = CommandData::Object();
                data.Set("elapsedMs", CommandData::Int(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count()));
                data.Set("cycles", CommandData::Int(cycles));
                data.Set("normal", CommandData::Int(normal));
                data.Set("conflicts", CommandData::Int(conflicts));
                data.Set("deleted", CommandData::Int(deleted));
                data.Set("corrupt", CommandData::Int(corrupt));
                data.Set("forcedFlushes", CommandData::Int(forced));
                data.Set("initialUiAgeMs", CommandData::Int(firstAge));
                data.Set("sourceAndSavedScenePreserved", CommandData::Bool(true));
                data.Set("finalMaximumDimension", CommandData::Int(2));
                return Ok("TEXTURE_AUTOSAVE_SOAK_OK", std::move(data));
            }
            std::string failure;
            experiment::cooked::TextureImportSettings settings;
            if (!experiment::cooked::ParseTextureImportSettings(baseline, settings, failure))
            {
                return Fail("texture.soak.baseline", failure);
            }
            settings.maxDimension = 0u;
            database.QueueAutomaticTextureSettings(source, settings);
            settings.maxDimension = 2u;
            database.QueueAutomaticTextureSettings(source, settings);
            const auto queued = database.PendingAutomaticSaveAge(sidecar);
            if (!queued || queued->count() >= 400 || read(sidecar) != baseline)
            {
                return Fail("texture.soak.queue", "Coalesced edits did not precede disk mutation");
            }
            external = baseline;
            if (kind == 2u)
            {
                std::error_code error;
                if (!std::filesystem::remove(sidecar, error) || error)
                {
                    return Fail("texture.soak.delete", "Could not delete isolated metadata");
                }
                external.clear();
            }
            else if (kind != 0u)
            {
                if (kind == 1u)
                {
                    auto document = Authoring::WriteDocument::ParseFile(sidecar, &failure);
                    if (!document)
                    {
                        return Fail("texture.soak.external", failure);
                    }
                    settings.maxDimension = 1u;
                    EditorAssetDatabase::WriteTextureImportSettings(document->Root(), settings);
                    external = document->Dump();
                }
                else
                {
                    external = "guid: 33333333-3333-4333-8333-333333333333\nimportSettings: [\n";
                }
                std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
                output << external;
                output.close();
                if (!output || read(sidecar) != external)
                {
                    return Fail("texture.soak.external", "Controlled metadata write failed");
                }
            }
            // Alternate complete four-mode rounds, so each kind sees both paths.
            if ((cycles / 4u) % 2u != 0u)
            {
                database.FlushAutomaticSaves();
                ++forced;
            }
            phase = 1;
            step = now;
            return std::nullopt;
        }
        if (age)
        {
            return std::nullopt;
        }
        if (phase == 1 && kind != 0u)
        {
            if (read(sidecar) != external || (kind == 2u && std::filesystem::exists(sidecar)) || database.AutomaticSaveError(sidecar).empty())
            {
                return Fail("texture.soak.overwrite", "Pending save lost the external state or rejection diagnostic");
            }
            std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
            output << baseline;
            output.close();
            if (!output || read(sidecar) != baseline)
            {
                return Fail("texture.soak.restore", "Could not restore baseline metadata");
            }
            experiment::cooked::TextureImportSettings settings;
            std::string failure;
            if (!experiment::cooked::ParseTextureImportSettings(baseline, settings, failure))
            {
                return Fail("texture.soak.restore", failure);
            }
            database.QueueAutomaticTextureSettings(source, settings);
            phase = 2;
            step = now;
            return std::nullopt;
        }
        experiment::cooked::TextureImportSettings settings;
        std::string failure;
        if (!database.AutomaticSaveError(sidecar).empty() ||
            !experiment::cooked::ParseTextureImportSettings(read(sidecar), settings, failure) || settings.maxDimension != 2u)
        {
            return Fail("texture.soak.recovery", "Normal/recovery save did not clear the error and save latest settings");
        }
        baseline = read(sidecar);
        ++cycles;
        normal += kind == 0u;
        conflicts += kind == 1u;
        deleted += kind == 2u;
        corrupt += kind == 3u;
        if (cycles % 20u == 0u)
        {
            std::printf("[TEXTURE_SOAK] cycles=%u elapsedMs=%lld\n", cycles,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count()));
            std::fflush(stdout);
        }
        phase = 0;
        step = now;
        return std::nullopt;
    }, true);
    return Ok("Texture soak armed; edit Maximum Dimension to 2 in the Inspector");
}
