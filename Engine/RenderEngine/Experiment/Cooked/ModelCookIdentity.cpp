#include "ModelCookIdentity.h"
#include "AuthoringParsedDocument.h"

#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(std::vector<ModelIdentityIssue>& issues,
            ModelIdentityIssueCode code, std::string context,
            std::string message)
        {
            issues.push_back(ModelIdentityIssue{
                code, std::move(context), std::move(message) });
        }

        [[nodiscard]] bool ReadScalar(const Authoring::ReadNode& parent,
            const char* key, const std::string& context, std::string& out,
            std::vector<ModelIdentityIssue>& issues)
        {
            const Authoring::ReadNode node = parent[key];
            if (!node)
            {
                AddIssue(issues, ModelIdentityIssueCode::MissingField,
                    context + "." + key, "필수 scalar가 없다.");
                return false;
            }
            if (!node.IsScalar())
            {
                AddIssue(issues, ModelIdentityIssueCode::InvalidDocument,
                    context + "." + key, "scalar여야 한다.");
                return false;
            }
            out = node.AsString();
            return true;
        }

        [[nodiscard]] bool ReadGuid(const Authoring::ReadNode& parent,
            const char* key, const std::string& context, AssetId& out,
            std::vector<ModelIdentityIssue>& issues)
        {
            std::string text;
            if (!ReadScalar(parent, key, context, text, issues)) return false;
            if (!TryParseCanonicalAssetId(text, out))
            {
                AddIssue(issues, ModelIdentityIssueCode::InvalidAssetId,
                    context + "." + key,
                    "소문자 canonical UUIDv4 asset identity여야 한다.");
                return false;
            }
            return true;
        }
    }

    bool ReadAssetIdFromMeta(std::string_view yaml, AssetId& outAssetId,
        std::vector<ModelIdentityIssue>& outIssues)
    {
        std::string parseError;
        const Authoring::ParsedDocument document =
            Authoring::ParsedDocument::ParseText(std::string(yaml), parseError);
        if (!document)
        {
            AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
                "meta", parseError);
            return false;
        }
        const Authoring::ReadNode root = document.Root();
        if (!root.IsMap())
        {
            AddIssue(outIssues, ModelIdentityIssueCode::InvalidDocument,
                "meta", "sidecar root는 map이어야 한다.");
            return false;
        }

        AssetId parsed{};
        if (!ReadGuid(root, "guid", "meta", parsed, outIssues)) return false;
        outAssetId = parsed;
        return true;
    }
}
