#pragma once

#include "ImportedScene.h"
#include "SceneToModelDraft.h"
#include "../ModelLoader.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

// 임포터를 ModelLoader 에 꽂는 접착제.
//
// ★ 이것이 없어서 임포터와 게시 경계가 이어져 있지 않았다. IModelDecoder 의
//   유일한 구현체가 검사 하네스(미리 만든 draft 를 복사해 돌려주는 것)였고,
//   검사는 임포트 → 변환 → 게시를 손으로 이어 붙였다. 생산 경로에는 그
//   접착제가 아예 없었다.
//
//   `파일 → IAssetImporter → ImportedScene → ConvertToModelDraft → ModelDraft`
//   까지를 한 덩어리로 묶어 ModelLoader 가 검증·게시만 하면 되게 한다.
namespace experiment::importer
{
    struct ImporterDecoderOptions final
    {
        ImportOptions import{};
        ConversionOptions conversion{};

        // ★ 모델 정체성은 .meta 소유다(ConversionOptions 주석과 같은 규약).
        //   경로마다 달라지므로 고정값이 아니라 정책으로 받는다. 설정하면
        //   conversion.modelAssetId 를 덮어쓴다.
        //
        //   비워 두면 conversion.modelAssetId 를 그대로 쓰고, 그것도 nil 이면
        //   게시 검증이 InvalidAssetIdentity 로 막는다 — 디코더가 정체성을
        //   지어내지 않는다는 뜻이다.
        std::function<AssetId(const std::filesystem::path&)> resolveModelAsset{};

        // 비워 두면 sourcePath 의 stem 을 쓴다.
        std::function<std::string(const std::filesystem::path&)> resolveModelName{};
    };

    class ImporterModelDecoder final : public IModelDecoder
    {
    public:
        // 기본 생성은 glTF·FBX 임포터를 등록한다.
        explicit ImporterModelDecoder(ImporterDecoderOptions options = {});

        // 임포터를 직접 주입한다(검사에서 한 포맷만 태우고 싶을 때).
        ImporterModelDecoder(ImporterDecoderOptions options,
            std::vector<std::unique_ptr<IAssetImporter>> importers);

        [[nodiscard]] ModelDecodeResult Decode(
            const ModelLoadRequest& request) override;

        // 이 디코더가 다룰 수 있는 확장자인지. 호출자가 미리 거를 때 쓴다.
        [[nodiscard]] bool CanDecode(
            const std::filesystem::path& sourcePath) const;

    private:
        ImporterDecoderOptions options_{};
        std::vector<std::unique_ptr<IAssetImporter>> importers_{};
    };

    // ImportNote → ModelLoadIssue. 코드 표가 서로 달라 억지로 사상하면 로그가
    // 거짓말을 하므로(예: OriginalAxisConverted 를 InvalidVertexAttribute 로),
    // 원래 코드 이름을 message 앞에 붙여 보존하고 심각도만 옮긴다.
    [[nodiscard]] ModelLoadIssue ToLoadIssue(const ImportNote& note);
}
