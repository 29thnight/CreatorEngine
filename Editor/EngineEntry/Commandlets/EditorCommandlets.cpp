#include "EditorCommandlets.h"
#include "../CommandCore/CommandRegistry.h"
#include "../../RenderTests/ExperimentParity/ExperimentMaterialMigrateSelfTest.h"
#include "../../RenderTests/ExperimentParity/ExperimentMaterialResolveSelfTest.h"
#include "../../RenderTests/ExperimentParity/ExperimentMaterialScriptSelfTest.h"

#include <exception>

namespace EditorCommandlets
{
    CommandCore::CommandResult Run(const std::vector<std::string>& arguments)
    {
        using namespace CommandCore;
        if (arguments.empty()) return InvalidArguments("--commandlet requires a name", "commandlet.name_missing");
        const std::string& name = arguments.front();
        using Test = bool (*)(std::string&);
        struct Entry { const char* name; Test synthetic; Test real; };
        const Entry entries[] = {
            {"experiment.matmigrate", &RenderTest::RunExperimentMaterialMigrateSelfTest, &RenderTest::RunExperimentMaterialMigrateReal},
            {"experiment.matresolve", &RenderTest::RunExperimentMaterialResolveSelfTest, &RenderTest::RunExperimentMaterialResolveReal},
            {"experiment.matscript", &RenderTest::RunExperimentMaterialScriptSelfTest, &RenderTest::RunExperimentMaterialScriptReal}
        };
        if (name == "list")
        {
            if (arguments.size() != 1) return InvalidArguments("This commandlet accepts no arguments", "commandlet.arguments");
            CommandData data = CommandData::Object();
            CommandData names = CommandData::Array();
            for (const auto& entry : entries) names.Append(CommandData::String(entry.name));
            for (const auto& entry : CommandCore::CommandRegistry::Commandlets().Sorted())
                names.Append(CommandData::String(entry.canonical));
            data.Set("names", std::move(names));
            return Ok("commandlets", std::move(data));
        }
        for (const auto& entry : entries)
        {
            if (name != entry.name) continue;
            // ★ 인자 판정은 이름이 맞은 뒤에 한다. 앞에서 크기부터 보면 등록되지
            //   않은 이름에 인자가 붙었을 때 "accepts no arguments" 가 나와 원인이
            //   "이름이 없다" 는 사실을 가린다(lifecycle.stress 은퇴가 그렇게 숨었다).
            if (arguments.size() != 1) return InvalidArguments("This commandlet accepts no arguments", "commandlet.arguments");
            try
            {
                std::string log;
                const bool synthetic = entry.synthetic(log);
                const bool real = entry.real(log);
                CommandData data = CommandData::Object();
                data.Set("name", CommandData::String(name));
                data.Set("synthetic", CommandData::Bool(synthetic));
                data.Set("real", CommandData::Bool(real));
                data.Set("log", CommandData::String(std::move(log)));
                return synthetic && real ? Ok(name, std::move(data))
                    : Fail("commandlet.failed", name, std::move(data));
            }
            catch (const std::exception& e) { return InternalError("commandlet.exception", e.what()); }
            catch (...) { return InternalError("commandlet.exception", "Unhandled commandlet exception"); }
        }
        return InvalidArguments("Unknown commandlet: " + name, "commandlet.unknown");
    }
}
