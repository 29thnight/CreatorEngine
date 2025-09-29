#include "VisualShaderDSL.h"

#include <regex>
#include "StringHelper.h"

bool ParseVisualShaderDSL(std::string_view srcView, VisualShaderGraphDesc& out)
{
    std::string src(srcView);

    static const std::regex lineComments(R"VSG(//[^\r\n]*)VSG");
    src = std::regex_replace(src, lineComments, "");

    static const std::regex blockComments(R"VSG(/\*[\s\S]*?\*/)VSG");
    src = std::regex_replace(src, blockComments, "");

    std::smatch headerMatch;
    static const std::regex headerWithName(R"VSG(VisualShader\s*(?:"([^"]+)")?\s*:\s*([A-Za-z0-9_.-]+)\s*\{)VSG");
    if (std::regex_search(src, headerMatch, headerWithName))
    {
        if (headerMatch[1].matched)
        {
            out.name = Trim(headerMatch[1]);
        }
        out.tag = Trim(headerMatch[2]);
    }
    else
    {
        return false;
    }

    const auto l = src.find('{');
    const auto r = src.rfind('}');
    if (l == std::string::npos || r == std::string::npos || r <= l)
    {
        return false;
    }

    const std::string body = src.substr(l + 1, r - l - 1);

    static const std::regex shaderAssetRegex(R"VSG(ShaderAsset\s*=\s*"([^"]+)")VSG");
    if (std::regex_search(body, headerMatch, shaderAssetRegex))
    {
        out.shaderAssetPath = Trim(headerMatch[1]);
    }

    static const std::regex queueRegex(R"VSG((Queue|queue|Tag|tag)\s*=\s*"([^"]*)")VSG");
    if (std::regex_search(body, headerMatch, queueRegex))
    {
        out.queueTag = Trim(headerMatch[2]);
    }

    static const std::regex keywordsRegex(R"VSG((Keywords|KEYWORDS|keywords)\s*=\s*\[\s*([^\]]*)\])VSG");
    if (std::regex_search(body, headerMatch, keywordsRegex))
    {
        const std::string payload = headerMatch[2];
        static const std::regex keywordItem(R"VSG("([^"]+)")VSG");
        for (std::sregex_iterator it(payload.begin(), payload.end(), keywordItem), end; it != end; ++it)
        {
            out.keywords.emplace_back(Trim((*it)[1]));
        }
    }

    static const std::regex nodeRegex(
        R"VSG(Node\s+"?([A-Za-z0-9_]+)"?\s+Type\s*=\s*"([A-Za-z0-9_.-]+)"\s*(?:\{([^}]*)\})?)VSG");
    for (std::sregex_iterator it(body.begin(), body.end(), nodeRegex), end; it != end; ++it)
    {
        VisualShaderNodeDesc node{};
        node.id = Trim((*it)[1]);
        node.type = Trim((*it)[2]);

        if ((*it)[3].matched)
        {
            const std::string props = (*it)[3];
            static const std::regex propRegex(R"VSG(([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)")VSG");
            for (std::sregex_iterator pit(props.begin(), props.end(), propRegex), pend; pit != pend; ++pit)
            {
                node.properties.emplace(ToLower(Trim((*pit)[1])), Trim((*pit)[2]));
            }
        }

        out.nodes.emplace_back(std::move(node));
    }

    static const std::regex connectRegex(R"VSG(Connect\s+"?([^"\s]+)"?\s*->\s*"?([^"\s]+)"?)VSG");
    for (std::sregex_iterator it(body.begin(), body.end(), connectRegex), end; it != end; ++it)
    {
        VisualShaderConnectionDesc connection{};
        if (!SplitEndpoint((*it)[1].str(), connection.fromNode, connection.fromSlot))
        {
            continue;
        }
        if (!SplitEndpoint((*it)[2].str(), connection.toNode, connection.toSlot))
        {
            continue;
        }
        out.connections.emplace_back(std::move(connection));
    }

    if (out.name.empty())
    {
        out.name = out.tag;
    }

    return !out.tag.empty();
}
