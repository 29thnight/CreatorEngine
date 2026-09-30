#pragma once

#include "CommandCore/CommandResult.h"
#include <string>
#include <vector>

class MeshRenderer;
struct EnhancedLiveViewRequest;

namespace editor::material_editing
{
bool Open(MeshRenderer& renderer, std::string& error);
void OnActiveSceneChanged();
void Draw();
void DrawInspectorPreview(MeshRenderer& renderer);
bool CapturePreviewRequest(EnhancedLiveViewRequest& request);
CommandCore::CommandResult Command(const std::vector<std::string>& parts);
} // namespace editor::material_editing
