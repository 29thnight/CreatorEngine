#pragma once

#include "../AssetCooker/FontAssetCookSupport.h"

namespace
{
    bool run_font_cook_contract(std::string& log)
    {
        const std::string latin = "11111111-1111-4111-8111-111111111111";
        const std::string korean = "22222222-2222-4222-8222-222222222222";
        const std::string caseGuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        font_cook::References references{ std::filesystem::path("C:/fixture/Assets"),
            { { "fonts/latin.ttf", latin }, { "fonts/korean.otf", korean },
              { "a/same.ttf", latin }, { "b/same.ttf", korean },
              { "collision.ttf", latin }, { "fonts/collision.ttf", korean } },
            { { latin, "Fonts/Latin.ttf" }, { korean, "Fonts/Korean.otf" },
              { caseGuid, "Fonts/Case.ttf" } } };
        std::size_t passed = 0;
        std::size_t failed = 0;
        const auto check = [&](bool condition, std::string_view message)
        {
            if (condition)
            {
                ++passed;
            }
            else
            {
                ++failed;
                log += "[FAIL] " + std::string(message) + "\n";
            }
        };
        std::string output;
        std::string error;
        check(font_cook::Resolve("", references, output, error) && output.empty(), "Default engine font");
        check(font_cook::Resolve(latin, references, output, error) && output == latin, "Registered GUID");
        check(font_cook::Resolve("AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA", references, output, error)
            && output == caseGuid, "Uppercase GUID serializes canonically like FileGuid");
        check(font_cook::Resolve("Fonts/Latin.ttf", references, output, error) && output == latin, "Project path to GUID");
        check(font_cook::Resolve("./Fonts/Latin.ttf", references, output, error) && output == latin, "Leading dot segment");
        check(font_cook::Resolve("Fonts/./Latin.ttf", references, output, error) && output == latin, "Qualified dot segment");
        check(font_cook::Resolve("FONTS\\KOREAN.OTF", references, output, error) && output == korean, "Case and separators");
        check(font_cook::Resolve("Latin.ttf", references, output, error) && output == latin, "Font-folder basename");
        check(font_cook::Resolve("Collision.ttf", references, output, error) && output == korean, "Basename never selects Assets-root collision");
        check(font_cook::Resolve("./Collision.ttf", references, output, error) && output == latin, "Qualified Assets-root identity remains distinct");
        check(font_cook::Resolve("A/same.ttf", references, output, error) && output == latin, "Qualified Latin identity");
        check(font_cook::Resolve("B/same.ttf", references, output, error) && output == korean, "Qualified Korean identity");
        check(!font_cook::Resolve("same.ttf", references, output, error), "No ambiguous global filename scan");
        check(!font_cook::Resolve("C:/outside/font.ttf", references, output, error), "External absolute path rejected");
        check(!font_cook::Resolve("../Fonts/Latin.ttf", references, output, error), "Parent traversal rejected");
        check(!font_cook::Resolve("Fonts/Missing.ttf", references, output, error), "Missing project font rejected");
        check(!font_cook::Resolve("33333333-3333-4333-8333-333333333333", references, output, error), "Unknown or wrong-kind GUID rejected");
        check(!font_cook::Resolve("Legacy.spritefont", references, output, error), "Legacy bitmap font rejected");
        check(font_cook::Resolve("NanumGothic-Regular.ttf", references, output, error)
            && output == "Fonts/Runtime/NanumGothic-Regular.ttf", "Bundled Hangul alias is portable");

        Authoring::WriteDocument document;
        document.Root().Child("TextComponent").SetScalar("1");
        document.Root().Child("fontPath").SetString("Fonts/Korean.otf");
        check(font_cook::RewriteNode(document.Root(), references, error)
            && document.Root().Read()["fontPath"].Scalar() == korean, "TextComponent path rewrite");
        document.Root().Child("m_typeUUID").SetString("another-component");
        document.Root().Child("fontPath").SetString("not-a-font");
        check(font_cook::RewriteNode(document.Root(), references, error)
            && document.Root().Read()["fontPath"].Scalar() == "not-a-font", "Component UUID is authoritative");

        Authoring::WriteDocument bundle;
        bundle.Root().Child("assetTypeID").SetScalar("3");
        bundle.Root().Child("assetName").SetString("Fonts/Latin.ttf");
        check(font_cook::RewriteNode(bundle.Root(), references, error)
            && bundle.Root().Read()["assetName"].Scalar() == latin, "Serialized font asset type 3 survives");

        Authoring::WriteDocument overrideValue;
        overrideValue.Root().SetString("Fonts/Korean.otf");
        std::string envelope;
        check(Authoring::EncodeCookedDocumentTextEnvelope(overrideValue.Root().Read(), envelope, error), "Override fixture encodes");
        Authoring::WriteDocument overrideDocument;
        overrideDocument.Root().Child("m_componentType").SetString("TextComponent");
        overrideDocument.Root().Child("m_propertyName").SetString("fontPath");
        overrideDocument.Root().Child("m_valueYaml").SetString(envelope);
        check(font_cook::RewriteNode(overrideDocument.Root(), references, error), "Prefab font override rewrites");
        auto decoded = Authoring::DecodeCookedDocumentTextEnvelope(
            overrideDocument.Root().Read()["m_valueYaml"].Scalar(), error);
        check(decoded && decoded->Root().Read().Scalar() == korean, "Prefab override retains canonical GUID");

        Authoring::WriteDocument malformed;
        malformed.Root().Child("TextComponent").SetScalar("1");
        malformed.Root().Child("fontPath").SetSequence();
        check(!font_cook::RewriteNode(malformed.Root(), references, error), "Non-scalar font rejected");
        log += "fontcook checks=" + std::to_string(passed + failed) + " passed=" + std::to_string(passed)
            + " failed=" + std::to_string(failed) + "\n";
        return failed == 0;
    }
}
