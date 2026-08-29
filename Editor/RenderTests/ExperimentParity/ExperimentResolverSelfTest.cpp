#include "ExperimentParity/ExperimentResolverSelfTest.h"

#include "Experiment/Cooked/ResolvingModelDecoder.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace ck = experiment::cooked;

        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        // 호출 여부와 결과를 마음대로 정하는 가짜 decoder.
        //
        // ★ 호출 **계수**를 센다. "결과가 맞다"만 보면 안 불려야 할 decoder 가
        //   불려도 결과가 우연히 같을 수 있다.
        class StubDecoder final : public ex::IModelDecoder
        {
        public:
            StubDecoder(bool producesDraft, std::string tag)
                : producesDraft_(producesDraft), tag_(std::move(tag)) {}

            ex::ModelDecodeResult Decode(const ex::ModelLoadRequest&) override
            {
                ++calls_;
                ex::ModelDecodeResult result;
                result.issues.push_back(ex::ModelLoadIssue{
                    ex::ModelLoadIssueSeverity::Warning,
                    ex::ModelLoadIssueCode::CookedPayloadRejected,
                    tag_, tag_ + " 가 남긴 사유" });
                if (producesDraft_)
                {
                    ex::ModelDraft draft;
                    draft.metadata.name = tag_;
                    result.draft = std::move(draft);
                }
                return result;
            }

            [[nodiscard]] std::size_t Calls() const noexcept { return calls_; }

        private:
            bool producesDraft_{};
            std::string tag_{};
            std::size_t calls_{};
        };

        [[nodiscard]] std::size_t CountIssues(
            const ex::ModelDecodeResult& result, ex::ModelLoadIssueCode code)
        {
            return static_cast<std::size_t>(std::ranges::count_if(result.issues,
                [code](const ex::ModelLoadIssue& issue)
                {
                    return issue.code == code;
                }));
        }

        [[nodiscard]] bool HasContext(const ex::ModelDecodeResult& result,
            std::string_view context)
        {
            return std::ranges::any_of(result.issues,
                [context](const ex::ModelLoadIssue& issue)
                {
                    return issue.context == context;
                });
        }

        [[nodiscard]] ex::ModelLoadRequest MakeRequest(
            ex::ModelSourcePreference preference,
            bool withCookedPath = true, bool withSourcePath = true)
        {
            ex::ModelLoadRequest request;
            request.sourcePreference = preference;
            if (withCookedPath) request.cookedPath = "probe.cemc";
            if (withSourcePath) request.sourcePath = "probe.glb";
            return request;
        }
    }

    bool RunExperimentResolverSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.resolver] 합성 검사\n";

        using Preference = ex::ModelSourcePreference;

        // ── 1. cooked 성공 → source 를 부르지 않는다 ────────────────────
        {
            auto cooked = std::make_unique<StubDecoder>(true, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* cookedRaw = cooked.get();
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedThenSource));
            check.Check(result.draft.has_value(), "cooked 성공 — draft 가 나와야 한다");
            check.Check(result.draft && result.draft->metadata.name == "cooked",
                "cooked 성공 — cooked 의 draft 여야 한다");
            check.Check(cookedRaw->Calls() == 1u, "cooked 성공 — cooked 1회 호출");
            // ★ 여기가 핵심이다. source 가 불리면 cooked 가 무의미해진다.
            check.Check(sourceRaw->Calls() == 0u, "cooked 성공 — source 는 부르지 않는다");
            check.Check(CountIssues(result,
                ex::ModelLoadIssueCode::CookedFallbackToSource) == 0u,
                "cooked 성공 — 폴백 기록이 없어야 한다");
            check.Check(HasContext(result, "cooked"),
                "cooked 성공 — cooked 의 note 를 보존한다");
        }

        // ── 2. cooked 거부 → source 폴백, 그리고 관측 가능 ──────────────
        {
            auto cooked = std::make_unique<StubDecoder>(false, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* cookedRaw = cooked.get();
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedThenSource));
            check.Check(result.draft.has_value(), "폴백 — draft 가 나와야 한다");
            check.Check(result.draft && result.draft->metadata.name == "source",
                "폴백 — source 의 draft 여야 한다");
            check.Check(cookedRaw->Calls() == 1u, "폴백 — cooked 를 먼저 시도한다");
            check.Check(sourceRaw->Calls() == 1u, "폴백 — source 1회 호출");
            // ★ **폴백은 반드시 한 줄로 남아야 한다.** 이게 없으면 cooked 가
            //   늘 거부되는데 조용히 도는 상태를 아무도 못 본다.
            check.Check(CountIssues(result,
                ex::ModelLoadIssueCode::CookedFallbackToSource) == 1u,
                "폴백 — 폴백 기록이 정확히 한 줄이어야 한다");
            // ★ 거부 사유를 지우지 않는다. 왜 폴백했는지가 거기 있다.
            check.Check(HasContext(result, "cooked"),
                "폴백 — cooked 의 거부 사유를 보존한다");
            check.Check(HasContext(result, "source"),
                "폴백 — source 의 note 도 보존한다");
        }

        // ── 3. SourceOnly — cooked 를 아예 부르지 않는다 ────────────────
        {
            auto cooked = std::make_unique<StubDecoder>(true, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* cookedRaw = cooked.get();
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::SourceOnly));
            check.Check(result.draft && result.draft->metadata.name == "source",
                "SourceOnly — source 의 draft");
            check.Check(cookedRaw->Calls() == 0u,
                "SourceOnly — cooked 를 부르지 않는다");
            check.Check(sourceRaw->Calls() == 1u, "SourceOnly — source 1회 호출");
            check.Check(CountIssues(result,
                ex::ModelLoadIssueCode::CookedFallbackToSource) == 0u,
                "SourceOnly — 폴백이 아니다");
        }

        // ── 4. CookedOnly 성공 / 실패 ──────────────────────────────────
        {
            auto cooked = std::make_unique<StubDecoder>(true, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedOnly));
            check.Check(result.draft && result.draft->metadata.name == "cooked",
                "CookedOnly 성공 — cooked 의 draft");
            check.Check(sourceRaw->Calls() == 0u,
                "CookedOnly 성공 — source 를 부르지 않는다");
        }
        {
            auto cooked = std::make_unique<StubDecoder>(false, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedOnly));
            check.Check(!result.draft.has_value(),
                "CookedOnly 실패 — draft 가 없어야 한다");
            // ★ 폴백할 곳이 없다. source 로 새면 preference 를 뒤집는 것이다.
            check.Check(sourceRaw->Calls() == 0u,
                "CookedOnly 실패 — source 로 새면 안 된다");
            check.Check(std::ranges::any_of(result.issues,
                [](const ex::ModelLoadIssue& issue)
                {
                    return issue.severity == ex::ModelLoadIssueSeverity::Error;
                }), "CookedOnly 실패 — Error 를 남긴다");
        }

        // ── 5. cooked 경로가 비면 시도하지 않고 넘어간다 ────────────────
        {
            auto cooked = std::make_unique<StubDecoder>(true, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* cookedRaw = cooked.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result = resolver.Decode(
                MakeRequest(Preference::CookedThenSource, false, true));
            check.Check(cookedRaw->Calls() == 0u,
                "cooked 경로 없음 — cooked 를 부르지 않는다");
            check.Check(result.draft && result.draft->metadata.name == "source",
                "cooked 경로 없음 — source 로 간다");
            check.Check(CountIssues(result,
                ex::ModelLoadIssueCode::CookedFallbackToSource) == 1u,
                "cooked 경로 없음 — 그 사실을 남긴다");
        }

        // ── 6. decoder 부재는 조용히 넘어가지 않는다 ────────────────────
        {
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(nullptr, std::move(source));

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedThenSource));
            check.Check(result.draft.has_value(),
                "cooked decoder 없음 — source 로 간다");
            check.Check(sourceRaw->Calls() == 1u,
                "cooked decoder 없음 — source 1회 호출");
            check.Check(CountIssues(result,
                ex::ModelLoadIssueCode::MissingPreferredDecoder) == 1u,
                "cooked decoder 없음 — 설정 결함을 남긴다");
        }
        {
            auto cooked = std::make_unique<StubDecoder>(false, "cooked");
            ck::ResolvingModelDecoder resolver(std::move(cooked), nullptr);

            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedThenSource));
            check.Check(!result.draft.has_value(),
                "source decoder 없음 — draft 가 없다");
            check.Check(CountIssues(result,
                ex::ModelLoadIssueCode::MissingPreferredDecoder) == 1u,
                "source decoder 없음 — 설정 결함을 남긴다");
        }
        {
            ck::ResolvingModelDecoder resolver(nullptr, std::make_unique<StubDecoder>(true, "source"));
            const ex::ModelDecodeResult result =
                resolver.Decode(MakeRequest(Preference::CookedOnly));
            check.Check(!result.draft.has_value(),
                "CookedOnly + cooked decoder 없음 — draft 가 없다");
            check.Check(std::ranges::any_of(result.issues,
                [](const ex::ModelLoadIssue& issue)
                {
                    return issue.code
                        == ex::ModelLoadIssueCode::MissingPreferredDecoder
                        && issue.severity == ex::ModelLoadIssueSeverity::Error;
                }), "CookedOnly + cooked decoder 없음 — Error");
        }

        // ── 7. source 경로가 비면 폴백할 곳이 없다 ─────────────────────
        {
            auto cooked = std::make_unique<StubDecoder>(false, "cooked");
            auto source = std::make_unique<StubDecoder>(true, "source");
            StubDecoder* sourceRaw = source.get();
            ck::ResolvingModelDecoder resolver(std::move(cooked), std::move(source));

            const ex::ModelDecodeResult result = resolver.Decode(
                MakeRequest(Preference::CookedThenSource, true, false));
            check.Check(!result.draft.has_value(),
                "source 경로 없음 — draft 가 없다");
            check.Check(sourceRaw->Calls() == 0u,
                "source 경로 없음 — source 를 부르지 않는다");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
