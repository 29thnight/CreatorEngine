#pragma once

#include "../Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

namespace assets
{
    // v3 animation metadata only. CEMCv11 ModelAssetGeneration remains a
    // transitional mesh/render adapter, not the payload owner for this path.
    struct ModelAnimationDescriptor final
    {
        experiment::cooked::ModelDescriptorArtifact summary{};
        experiment::cooked::ResolvedAssetEntry origin{};
        // Loadable entries are captured metadata, not opened child files.
        // A typed entry with no byteSource records absence in this snapshot.
        experiment::cooked::ResolvedAssetEntry skeleton{};
        std::vector<experiment::cooked::ResolvedAssetEntry> clips{};
        std::size_t metadataBytes{};

        // Selected children open only in their payload workers; the resulting
        // payload origins retain exact file pins. Unselected locators retain the
        // source for BuildAssetSet's unique immutable release root, whose files
        // are independent of the cooker cache. Logical unmount deletes no files.
        // External deletion/replacement can cause I/O/integrity failure; captured
        // hashes prevent admitting changed bytes or using the latest resolver.
        // Future managed artifact GC must honor a store/root lifetime lease;
        // absence of open child files is not permission to delete a release.
        // No skeleton/clip bulk owner or per-child file handle is kept here.
        [[nodiscard]] std::size_t ByteSize() const noexcept { return metadataBytes; }
    };
}
