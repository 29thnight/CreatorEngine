#include "ImporterModelDecoder.h"

#include "FbxImporter.h"
#include "GltfImporter.h"

#include <utility>

namespace experiment::importer
{
    namespace
    {
        // 이름이 다른 TU 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가 두 TU 를
        // 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다.
        [[nodiscard]] ModelLoadIssueSeverity ToLoadSeverity(
            ImportNoteSeverity severity) noexcept
        {
            switch (severity)
            {
            case ImportNoteSeverity::Error:   return ModelLoadIssueSeverity::Error;
            case ImportNoteSeverity::Warning: return ModelLoadIssueSeverity::Warning;
            default:                          return ModelLoadIssueSeverity::Info;
            }
        }

        void AppendIssues(const std::vector<ImportNote>& notes,
            std::vector<ModelLoadIssue>& out)
        {
            out.reserve(out.size() + notes.size());
            for (const ImportNote& note : notes) out.push_back(ToLoadIssue(note));
        }

        [[nodiscard]] bool HasError(const std::vector<ModelLoadIssue>& issues) noexcept
        {
            for (const ModelLoadIssue& issue : issues)
            {
                if (issue.severity == ModelLoadIssueSeverity::Error) return true;
            }
            return false;
        }
    }

    ModelLoadIssue ToLoadIssue(const ImportNote& note)
    {
        ModelLoadIssue issue;
        issue.severity = ToLoadSeverity(note.severity);
        issue.code = ModelLoadIssueCode::ImportNote;
        issue.context = note.context;
        issue.message = "[" + std::string(ToString(note.code)) + "] " + note.message;
        if (note.count > 1)
        {
            issue.message += " (×" + std::to_string(note.count) + ")";
        }
        return issue;
    }

    ImporterModelDecoder::ImporterModelDecoder(ImporterDecoderOptions options)
        : options_(std::move(options))
    {
        importers_.push_back(std::make_unique<GltfImporter>());
        importers_.push_back(std::make_unique<FbxImporter>());
    }

    ImporterModelDecoder::ImporterModelDecoder(ImporterDecoderOptions options,
        std::vector<std::unique_ptr<IAssetImporter>> importers)
        : options_(std::move(options))
        , importers_(std::move(importers))
    {
    }

    bool ImporterModelDecoder::CanDecode(
        const std::filesystem::path& sourcePath) const
    {
        for (const std::unique_ptr<IAssetImporter>& importer : importers_)
        {
            if (importer && importer->CanImport(sourcePath)) return true;
        }
        return false;
    }

    ModelDecodeResult ImporterModelDecoder::Decode(const ModelLoadRequest& request)
    {
        ModelDecodeResult result;

        // 이 디코더는 source 만 다룬다. cooked 코덱은 별도 구현이고
        // (ModelPayloadKind::Cooked), 그것이 없는 상태에서 CookedOnly 를 받으면
        // 조용히 source 로 대체하지 않고 실패로 보고한다.
        if (request.sourcePreference == ModelSourcePreference::CookedOnly)
        {
            result.issues.push_back({ ModelLoadIssueSeverity::Error,
                ModelLoadIssueCode::MissingDecoder, "request.sourcePreference",
                "CookedOnly 를 요청했지만 이 디코더는 source 임포터다 — "
                "cooked 코덱이 아직 없다." });
            return result;
        }
        if (request.sourcePath.empty())
        {
            result.issues.push_back({ ModelLoadIssueSeverity::Error,
                ModelLoadIssueCode::EmptyRequestPath, "request.sourcePath",
                "source 경로가 비어 있다." });
            return result;
        }

        IAssetImporter* chosen = nullptr;
        for (const std::unique_ptr<IAssetImporter>& importer : importers_)
        {
            if (importer && importer->CanImport(request.sourcePath))
            {
                chosen = importer.get();
                break;
            }
        }
        if (!chosen)
        {
            result.issues.push_back({ ModelLoadIssueSeverity::Error,
                ModelLoadIssueCode::MissingDecoder, "request.sourcePath",
                "이 확장자를 다루는 임포터가 없다: "
                + request.sourcePath.extension().string() });
            return result;
        }

        ImportRequest importRequest;
        importRequest.sourcePath = request.sourcePath;
        importRequest.options = options_.import;

        const ImportResult imported = chosen->Import(importRequest);
        AppendIssues(imported.notes, result.issues);
        if (!imported.Succeeded())
        {
            // 임포터가 Error 노트를 남기지 않고 실패했을 수 있다. 그러면 위
            // 전달분만으로는 실패 이유가 로그에 없으므로 한 건을 보탠다.
            if (!HasError(result.issues))
            {
                result.issues.push_back({ ModelLoadIssueSeverity::Error,
                    ModelLoadIssueCode::DecoderFailure, "importer",
                    "임포터가 ImportedScene 을 만들지 못했다." });
            }
            return result;
        }

        // ★ 정체성은 받는다. 정책이 없으면 conversion.modelAssetId 를 그대로
        //   쓰고, 그것도 nil 이면 게시 검증이 막는다 — 여기서 지어내지 않는다.
        ConversionOptions conversion = options_.conversion;
        if (options_.resolveModelAsset)
        {
            conversion.modelAssetId = options_.resolveModelAsset(request.sourcePath);
        }
        if (conversion.modelName.empty())
        {
            conversion.modelName = options_.resolveModelName
                ? options_.resolveModelName(request.sourcePath)
                : request.sourcePath.stem().string();
        }

        ConversionResult converted =
            ConvertToModelDraft(*imported.scene, conversion);
        AppendIssues(converted.notes, result.issues);
        if (!converted.Succeeded())
        {
            if (!HasError(result.issues))
            {
                result.issues.push_back({ ModelLoadIssueSeverity::Error,
                    ModelLoadIssueCode::DecoderFailure, "conversion",
                    "변환 경계가 ModelDraft 를 만들지 못했다." });
            }
            return result;
        }

        result.draft = std::move(converted.draft);
        return result;
    }
}
