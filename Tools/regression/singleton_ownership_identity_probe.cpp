#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Distribution.h"
#else
import Creator.Core.Distribution;
#endif
#include "../../Engine/RenderEngine/Assets/ClusterStreaming.h"
#include "../../Engine/RenderEngine/RHI/RHIRecordingDiagnostics.h"
#include "../../Editor/EngineGUIWindow/InspectorIcon.h"

const void* page_io_from_other_unit() { return &assets::cluster::PageIo::Shared(); }
const void* policy_from_other_unit() { return &RHIRecordingPolicy::Get(); }
const void* identity_from_other_unit() { return &ce::distribution::current_identity(); }
const void* icon_table_from_other_unit() { return &editor::inspector::inspector_icon_table(); }
const char* lookup_icon_from_other_unit(std::size_t key) { return editor::inspector::inspector_icon(key); }
