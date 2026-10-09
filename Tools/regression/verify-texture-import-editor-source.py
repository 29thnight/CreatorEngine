#!/usr/bin/env python3
"""PHASE 12 texture import source contracts; never starts the editor or compiler.

Authored for later explicit execution. These structural tripwires do not replace
Windows editor edit/reimport, failure rollback, rename and old-owner lifetime tests.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class TextureImportEditorContracts(unittest.TestCase):
    def test_typed_settings_preserve_identity_and_exclude_sampler(self):
        database = source("Editor/EngineEntry/EditorAssetDatabase.cpp")
        writer = database.split("void EditorAssetDatabase::WriteTextureImportSettings(", 1)[1]
        writer = writer.split("bool EditorAssetDatabase::SetTextureImportSettingsAndReimport", 1)[0]
        for field in ("colorSpace", "compression", "mipPolicy", "maxDimension", "normalMap",
                      "preserveAlphaCoverage", "alphaCutoff", "compressionQuality"):
            self.assertIn(f'Child("{field}")', writer)
        for forbidden in ('Child("guid")', 'Child("wrap")', 'Child("filter")'):
            self.assertNotIn(forbidden, writer)
        inspector = source("Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp")
        self.assertIn('"Save and Reimport"', inspector)
        self.assertIn("ValidateTextureImportSettings(settings, validationError)", inspector)
        self.assertIn("SetTextureImportSettingsAndReimport(source, settings", inspector)
        self.assertIn("TextureImportReady(source)", inspector)
        self.assertIn("current scene keeps its prior textures until Reload Saved Scene", database)
        self.assertIn("Unsaved changes in the active scene will be discarded", inspector)
        self.assertIn('EnqueueStructured({ "scene.open_async", scene.string() })', inspector)
        self.assertNotIn("QueueSceneLoad(", inspector)

    def test_cook_is_scheduled_and_never_waited_on_game_thread(self):
        database = source("Editor/EngineEntry/EditorAssetDatabase.cpp")
        importer = database.split("struct TextureImportWork final", 1)[1]
        importer = importer.split("bool SetModelMeshletsAndReimport", 1)[0]
        self.assertIn("get_job_scheduler().submit_after", importer)
        self.assertIn("self->CookTextureGeneration", importer)
        self.assertIn("CaptureArtifactSource(publication->byteSource", importer)
        self.assertIn("DataSystems->QueueAssetChange", importer)
        self.assertNotIn(".wait(", importer)
        self.assertNotIn("std::thread", importer)
        self.assertNotIn("std::mutex", importer)

    def test_old_work_is_superseded_and_publication_uses_existing_mount(self):
        database = source("Editor/EngineEntry/EditorAssetDatabase.cpp")
        moved = database.split("void HandleMoved(", 1)[1].split("bool TargetStillExists", 1)[0]
        self.assertIn("InvalidateTextureImportLocked(oldSource)", moved)
        self.assertIn("ReloadChangedTexture(newSource)", moved)
        self.assertIn("ReloadChangedTexture(newPath)", moved)
        runtime = source("Engine/RenderEngine/AssetDepot/AssetDepot.cpp")
        publication = runtime.split("bool DataSystem::PublishAuthoredTexture", 1)[1]
        publication = publication.split("namespace AssetDepot", 1)[0]
        self.assertLess(publication.index("latestRevision->load"), publication.index("MountAssetSet("))
        self.assertIn("options.overrideIdentities.push_back(reference.key)", publication)
        self.assertIn("options, issues, previousMount", publication)
        self.assertNotIn("UnmountAssetSet(", publication)
        replacement = runtime.split("AssetMountId DataSystem::MountAssetSet(", 1)[1]
        replacement = replacement.split("bool DataSystem::UnmountAssetSet(", 1)[0]
        self.assertLess(replacement.index("candidate.WithoutMountedAssetSet"), replacement.index("m_cookedCatalog = std::move(published)"))
        self.assertIn("RequestAsync<Texture>", publication)
        self.assertIn("kCookedTextureRepresentationVersion", runtime)

    def test_model_image_handoff_checks_blocks_before_allocation(self):
        runtime = source("Engine/RenderEngine/DataSystem.cpp")
        image = runtime.split("TextureImage BuildGenerationCpuImage(", 1)[1]
        image = image.split("DataSystem::ResolveModelGenerationTexture", 1)[0]
        self.assertIn("RHIFormat::BC7UnormSrgb", image)
        self.assertIn("RHIFormat::BC5Unorm", image)
        self.assertLess(image.index("texture.pixels.size() - source.offset"), image.index("TextureImage::Allocate"))
        self.assertIn("RHIFormatRowCount(texture.format, source.height)", image)
        self.assertNotIn("source.offset + source.slicePitch", image)

    def test_decal_bootstrap_uses_exact_sidecars_and_matching_variants(self):
        runtime = source("Engine/RenderEngine/DataSystem.cpp")
        discovery = runtime.split("const auto discoverDecalTexture =", 1)[1]
        discovery = discovery.split("std::function<void(const Authoring::ReadNode&", 1)[0]
        self.assertIn('field == "m_diffusefileName"', discovery)
        self.assertIn("TextureAssetColorSpace::Linear", discovery)
        self.assertIn("TextureFileType::Texture) + 1u", discovery)
        producer = source("Engine/RenderEngine/Experiment/Cooked/SceneCookProducer.cpp")
        self.assertIn('assetRoot / "Textures" / filename', producer)
        self.assertIn("maximumSidecarBytes", producer)
        self.assertIn("IsContainedPath(assetRoot, meta)", producer)
        self.assertIn("walk.AddEdge(texture, product.textureEdges, CookedAssetKind::Texture)", producer)
        self.assertIn("request.textureIdentityRoot", producer)
        bootstrap = source("BuildTool/RuntimeBootstrap.cs")
        self.assertIn("Metadata.Verify(textureIdentityRoot, textureIdentityEntries", bootstrap)
        self.assertIn("textureIdentityInputDigest", bootstrap)


if __name__ == "__main__":
    unittest.main()
