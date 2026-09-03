#pragma once

// MBC6 compatibility surface. 정점 레이아웃의 정본과 제품 namespace는
// Assets/ModelVertexLayout.h의 assets다. 남은 importer/cooked 호출부는 MBC9에서
// 제거될 때까지 using declaration만 거치며 표를 복제하지 않는다.
#include "../Assets/ModelVertexLayout.h"

namespace experiment
{
    using assets::VertexAttribute;
    using assets::VertexFormat;
    using assets::VertexAttributeDesc;
    using assets::VertexAttributeMask;
    using assets::SizeOf;
    using assets::Bit;
    using assets::Has;
    using assets::StrideOf;
    using assets::OffsetOf;
    using assets::DescOf;
    using assets::VertexLayoutHash;
    using assets::IsSupportedModelVertexLayout;
    using assets::kVertexAttributeTable;
    using assets::kAllVertexAttributes;
    using assets::kCoreVertexAttributes;
    using assets::kColorVertexAttributes;
    using assets::kSkinVertexAttributes;
    using assets::kRequiredVertexAttributes;
    using assets::kV2VertexAttributes;
    using assets::kModelVertexMasks;
    using assets::kCoreColorSkinVertexAttributes;
    using assets::kInvalidVertexOffset;
    using assets::kVertexLayoutTableHash;
}
