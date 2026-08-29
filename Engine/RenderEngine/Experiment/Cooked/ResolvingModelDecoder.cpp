#include "ResolvingModelDecoder.h"

#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(ModelDecodeResult& result, ModelLoadIssueSeverity severity,
            ModelLoadIssueCode code, std::string context, std::string message)
        {
            result.issues.push_back(ModelLoadIssue{ severity, code,
                std::move(context), std::move(message) });
        }

        void AppendIssues(ModelDecodeResult& into,
            std::vector<ModelLoadIssue> from)
        {
            into.issues.insert(into.issues.end(),
                std::make_move_iterator(from.begin()),
                std::make_move_iterator(from.end()));
        }
    }

    ResolvingModelDecoder::ResolvingModelDecoder(
        std::unique_ptr<IModelDecoder> cookedDecoder,
        std::unique_ptr<IModelDecoder> sourceDecoder) noexcept
        : cooked_(std::move(cookedDecoder))
        , source_(std::move(sourceDecoder))
    {
    }

    ModelDecodeResult ResolvingModelDecoder::Decode(
        const ModelLoadRequest& request)
    {
        ModelDecodeResult result;

        const bool wantsCooked =
            request.sourcePreference != ModelSourcePreference::SourceOnly;
        const bool wantsSource =
            request.sourcePreference != ModelSourcePreference::CookedOnly;

        // ── cooked ─────────────────────────────────────────────────────
        bool cookedAttempted = false;
        if (wantsCooked)
        {
            if (!cooked_)
            {
                // ★ 조용히 source 로 넘어가지 않는다. `CookedOnly` 는 물론이고
                //   `CookedThenSource` 에서도 "cooked decoder 가 없다"는 사실은
                //   설정 결함이지 폴백 사유가 아니다.
                AddIssue(result,
                    request.sourcePreference == ModelSourcePreference::CookedOnly
                        ? ModelLoadIssueSeverity::Error
                        : ModelLoadIssueSeverity::Warning,
                    ModelLoadIssueCode::MissingPreferredDecoder, "resolver.cooked",
                    "cooked decoder가 설치되지 않았다.");
            }
            else if (request.cookedPath.empty())
            {
                // cooked 경로가 없으면 시도할 것이 없다. `CookedOnly` 에서는
                // ModelLoader 가 이미 빈 경로를 거부하므로 여기 오지 않는다.
                AddIssue(result, ModelLoadIssueSeverity::Info,
                    ModelLoadIssueCode::CookedFallbackToSource, "resolver.cooked",
                    "cooked 경로가 비어 있어 source로 간다.");
            }
            else
            {
                cookedAttempted = true;
                ModelDecodeResult cooked = cooked_->Decode(request);
                if (cooked.draft.has_value())
                {
                    // cooked 가 남긴 note 는 버리지 않는다.
                    result.draft = std::move(cooked.draft);
                    AppendIssues(result, std::move(cooked.issues));
                    return result;
                }
                // ★ 거부 사유를 지우지 않는다. 왜 폴백했는지가 여기 있다.
                AppendIssues(result, std::move(cooked.issues));
            }
        }

        if (!wantsSource)
        {
            // CookedOnly 인데 여기까지 왔다 = cooked 가 못 냈다.
            AddIssue(result, ModelLoadIssueSeverity::Error,
                ModelLoadIssueCode::MissingDraft, "resolver.cooked",
                "CookedOnly인데 cooked decoder가 draft를 내지 못했다.");
            return result;
        }

        // ── source ─────────────────────────────────────────────────────
        if (!source_)
        {
            AddIssue(result, ModelLoadIssueSeverity::Error,
                ModelLoadIssueCode::MissingPreferredDecoder, "resolver.source",
                "source decoder가 설치되지 않았다.");
            return result;
        }
        if (request.sourcePath.empty())
        {
            AddIssue(result, ModelLoadIssueSeverity::Error,
                ModelLoadIssueCode::EmptyRequestPath, "resolver.source",
                "source 경로가 비어 있어 폴백할 곳이 없다.");
            return result;
        }

        if (cookedAttempted)
        {
            // ★ 폴백이 일어났다는 사실 자체를 남긴다. 위의 거부 사유와 별개다 —
            //   사유는 여러 개일 수 있지만 "폴백했다"는 한 줄이어야 세기 쉽다.
            AddIssue(result, ModelLoadIssueSeverity::Info,
                ModelLoadIssueCode::CookedFallbackToSource, "resolver",
                "cooked가 draft를 내지 못해 source로 폴백했다.");
        }

        ModelDecodeResult decoded = source_->Decode(request);
        result.draft = std::move(decoded.draft);
        AppendIssues(result, std::move(decoded.issues));
        return result;
    }
}
