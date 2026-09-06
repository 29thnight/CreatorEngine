#include "EditorObjectOperations.h"
#include "SceneViewWindow.h"
#include "ReflectionUndo.h"
#include "EditorCameraRig.h"
#include "RHI/ScreenSizedResource.h"
#include "MeshRenderer.h"
// RenderScene::UpdateCommand를 직접 부른다. 예전에는 다른 헤더를 타고
// 딸려 들어왔는데, 옥트리 계통을 걷으면서 그 경로가 끊겼다.
#include "RenderScene.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "GizmoRenderer.h"
#include "ImGuizmo.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "Scene.h"
#include "Camera.h"
#include "GameObjectCommand.h"
#include "CameraComponent.h"
#include "FoliageComponent.h"
#include "RectTransformComponent.h"
#include "LightComponent.h"
#include "Entity.h"
#include <unordered_map>
#include "DataSystem.h"
#include "RenderState.h"
#include "PrefabUtility.h"
#include "InputManager.h"
#include "Terrain.h"
#include "EditorSessionState.h"
#include "EditorAssetPresentation.h"
#include "RuntimeSettings.h"
#include "Mathematics.Intersect.h"

#include <cmath>
#include <cstring>
#include <mathematics/transform.hpp>

bool useWindow = true;
bool editWindow = true;
int gizmoCount = 1;
float camDistance = 8.f;
static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);


static const float identityMatrix[16] = {
	1.f, 0.f, 0.f, 0.f,
	0.f, 1.f, 0.f, 0.f,
	0.f, 0.f, 1.f, 0.f,
	0.f, 0.f, 0.f, 1.f
};

enum class SelectGuizmoMode
{
	Select,
	Translate,
	Rotate,
	Scale
};

bool RayIntersectsPlane(const Ray& ray, const math::vector3& planeNormal, const math::vector3& planePoint, float& outDistance)
{
	const float denom = math::dot(planeNormal, ray.direction);
	// 노멀과 평행하면 교차 없음
	if (std::fabs(denom) < 1e-6f)
		return false;

	const math::vector3 diff = planePoint - ray.origin;
	const float t = math::dot(diff, planeNormal) / denom;

	if (t < 0)
		return false;

	outDistance = t;
	return true;
}

SceneViewWindow::SceneViewWindow(EditorCameraRig* editorCameraRig, GizmoRenderer* gizmo_ptr) :
	m_editorCamera(editorCameraRig ? &editorCameraRig->GetCamera() : nullptr),
	m_editorCameraRig(editorCameraRig),
	m_gizmoRenderer(gizmo_ptr)
{
}

void SceneViewWindow::RenderSceneViewWindow()
{
	auto scene = SceneManagers->GetActiveScene();
	auto obj = scene->GetSelectedEntity();
	if (obj)
	{
		math::matrix4x4 objMat{};
		if (auto* rect = obj->GetComponent<RectTransformComponent>())
		{
			auto rectWorld = rect->GetWorldRect();
			objMat = math::translation_matrix(math::vector3{
				rectWorld.x + rectWorld.width * rect->GetPivot().x,
				rectWorld.y + rectWorld.height * rect->GetPivot().y,
				0.f });
		}
		else
		{
			objMat = obj->Transform_().GetWorldMatrix();
		}

		auto view = m_editorCamera->CalculateView();
		auto projection = m_editorCamera->CalculateProjection();

		RenderSceneView(&view.m[0][0], &projection.m[0][0],
			&objMat.m[0][0], true, obj, m_editorCamera);

	}
	else
	{
		auto view = m_editorCamera->CalculateView();
		auto projection = m_editorCamera->CalculateProjection();
		auto identity = math::matrix4x4::identity();

		RenderSceneView(&view.m[0][0], &projection.m[0][0], &identity.m[0][0], false, nullptr, m_editorCamera);
	}
}

// 최상위 오브젝트의 부모 월드 행렬은 항등이다.
//
// 부모를 지정하지 않고 만든 오브젝트(카메라·라이트·빈 오브젝트)는 m_parentIndex가
// INVALID_INDEX(-1)로 남고 씬 루트의 children으로만 매달린다 — 씬 전체가 쓰는
// 규약이다(Scene::AttachExistingEntity와 SceneManager 로더의 루트 children
// 재구성이 같은 규약을 쓴다). 그런 오브젝트에 대해 FindIndex는 널을 돌려준다.
//
// 예전에는 여기서 그 결과를 검사 없이 역참조했다. 널에 m_transform 오프셋을 더한
// 0xA0이 가짜 this가 되어 Transform::ResolveStore가 m_owner를 읽다 죽었다
// (2026-08-18 덤프). 슬롯맵 전환(트랙 E1) 전에는 무효 인덱스 조회가 조용히 씬
// 루트를 돌려줘서 이 결함이 가려져 있었다.
static math::matrix4x4 ResolveParentWorldMatrix(const Entity* obj)
{
	if (nullptr == obj) return math::matrix4x4::identity();

	Scene* scene = obj->GetScene();
	Entity* parent = scene ? scene->TryGetEntity(obj->GetParentIndex()) : nullptr;
	if (nullptr == parent) return math::matrix4x4::identity();

	return parent->Transform_().GetWorldMatrix();
}

void SceneViewWindow::RenderSceneView(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, Entity* obj, Camera* cam)
{
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);
	static bool useSnap = false;
	static float snap[3] = { 1.f, 1.f, 1.f };
	static float bounds[] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
	static float boundsSnap[] = { 0.1f, 0.1f, 0.1f };
	static bool boundSizing = false;
	static bool boundSizingSnap = false;
	static bool selectMode = false;
	static Entity* selected = nullptr;
	static enum class SelectGuizmoMode selectGizmoMode = SelectGuizmoMode::Translate;
	static const char* buttons[] = {
		ICON_FA_EYE,
		ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT,
		ICON_FA_ARROWS_ROTATE,
		ICON_FA_GROUP_ARROWS_ROTATE,
	};
	static const int buttonCount = sizeof(buttons) / sizeof(buttons[0]);


	ImGuizmo::SetOrthographic(m_editorCamera->m_isOrthographic);
	ImGuizmo::BeginFrame();
	bool ctrl = InputManagement->IsKeyPressed((int)KeyBoard::LeftControl);
	bool rightMouse = InputManagement->IsMouseButtonPressed(MouseKey::RIGHT) || ImGui::IsMouseDown(ImGuiMouseButton_Right);

	if(!ctrl && !rightMouse)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W))
			selectGizmoMode = SelectGuizmoMode::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E))
			selectGizmoMode = SelectGuizmoMode::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R)) // r Key
			selectGizmoMode = SelectGuizmoMode::Scale;
		if (ImGui::IsKeyPressed(ImGuiKey_T))
			useSnap = !useSnap;
		if (ImGui::IsKeyPressed(ImGuiKey_Q))
			selectGizmoMode = SelectGuizmoMode::Select;
	}

	ImGuiIO& io = ImGui::GetIO();
	float viewManipulateRight = io.DisplaySize.x;
	float viewManipulateTop = 0;
	float windowTopLeftX = 0;
	float windowTopLeftY = 0;
	ImVec2 imageMin{};
	ImVec2 imageMax{};
	float windowWidth = 0;
	float windowHeight = 0;

	static ImGuiWindowFlags gizmoWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (useWindow)
	{
		ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.f, 0.f, 0.f, 1.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
		ImGui::Begin(ICON_FA_USERS_VIEWFINDER "  Scene      ", 0, gizmoWindowFlags);
		ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
		ImGuizmo::SetDrawlist();

		windowWidth = (float)ImGui::GetWindowWidth();
		windowHeight = (float)ImGui::GetWindowHeight();
		windowTopLeftX = ImGui::GetWindowPos().x;
		windowTopLeftY = ImGui::GetWindowPos().y;
		float titleBarHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;
		ImGuizmo::SetRect(windowTopLeftX, windowTopLeftY + titleBarHeight, windowWidth, windowHeight);
		viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
		viewManipulateTop = ImGui::GetWindowPos().y;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		gizmoWindowFlags |= ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;

		float x = windowWidth;
		float y = windowHeight;

		auto scene = SceneManagers->GetRenderScene();

		// EnhancedRenderer가 유일한 표시 공급자다. 첫 GPU 프레임 전에는
		// 명시적인 준비 배경을 그리며 DX11 RenderPassData로 폴백하지 않는다.
		ImTextureID displayed = 0;
		if (const uint64_t liveTextureId =
			EnhancedSceneRenderer::GetLiveDisplayImTextureId(
				EnhancedLiveDisplayTarget::Editor))
		{
			displayed = (ImTextureID)liveTextureId;   // DX12 셸 — 공유 텍스처 직결
		}
		if (displayed != 0)
		{
			ImGui::Image(displayed, ImVec2(x, y));
		}
		else
		{
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max{ min.x + x, min.y + y };
			ImGui::InvisibleButton("##EnhancedRendererPending", ImVec2(x, y));
			ImGui::GetWindowDrawList()->AddRectFilled(min, max,
				ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.09f, 1.f)));
		}
		imageMin = ImGui::GetItemRectMin();
		imageMax = ImGui::GetItemRectMax();

		ImVec2 imagePos = ImGui::GetItemRectMin();
		ImGui::SetCursorScreenPos(ImVec2(imagePos.x + 5, imagePos.y + 5));

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
		if (ImGui::Button(ICON_FA_CHART_BAR))
		{
			ImGui::OpenPopup("RenderStatistics");
		}
		ImGui::PopStyleVar();

		ImGui::SameLine();
		ImVec2 currentPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 5, currentPos.y));
		if (ImGui::Button(ICON_FA_BARS " Grid"))
		{
			m_gizmoRenderer->m_bShowGridSettings = true;
		}

		ImGui::SameLine();

		currentPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 5, currentPos.y));

		if (ImGui::Button(m_editorCamera->m_isOrthographic ? ICON_FA_EYE_LOW_VISION " Orthographic" : ICON_FA_ARROWS_TO_EYE " Perspective"))
		{
			m_editorCamera->m_isOrthographic = !m_editorCamera->m_isOrthographic;
		}

		ImGui::SameLine();
		currentPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 5, currentPos.y));

		if (ImGui::Button(ICON_FA_CAMERA " Camera"))
		{
			ImGui::OpenPopup("CameraSettings");
		}



		ImGui::SameLine();
		currentPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(windowWidth - 270.f, currentPos.y));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
		for (int i = 0; i < buttonCount; i++)
		{
			if (i == (int)selectGizmoMode)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.1f, 0.9f, 0.8f));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
			}

			if (ImGui::Button(buttons[i]))
			{
				selectGizmoMode = (SelectGuizmoMode)i;
			}

			ImGui::SameLine();
			currentPos = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 1, currentPos.y));
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		currentPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 5, currentPos.y));
		if (useSnap)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.1f, 0.9f, 0.8f));
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
		}

		if (ImGui::Button(ICON_FA_BORDER_ALL " Snap"))
		{
			useSnap = !useSnap;
		}
		ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 1, currentPos.y));
		ImGui::PopStyleColor();

		ImGui::PopStyleVar(1);


		if (editTransformDecomposition)
		{
			switch (selectGizmoMode)
			{
			case SelectGuizmoMode::Select:
				selectMode = true;
				break;
			case SelectGuizmoMode::Translate:
				mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
				selectMode = false;
				break;
			case SelectGuizmoMode::Rotate:
				mCurrentGizmoOperation = ImGuizmo::ROTATE;
				selectMode = false;
				break;
			case SelectGuizmoMode::Scale:
				mCurrentGizmoOperation = ImGuizmo::SCALE;
				selectMode = false;
				break;
			default:
				break;
			}
		}

		ImGui::PopStyleVar(2);

		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 5.f);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.1f, 0.1f, 0.1f, 0.8f));
		ImGui::PushFont(EditorAssetPresentation::Get().GetSmallFont());
		if (ImGui::BeginPopup("CameraSettings"))
		{
			ImGui::Text("Camera Settings");
			ImGui::Separator();
			ImGui::InputFloat("FOV  ", &cam->m_fov);
			if (0 == cam->m_fov)
			{
				cam->m_fov = 1.f;
			}
			ImGui::InputFloat("Near Plane  ", &cam->m_nearPlane);
			ImGui::InputFloat("Far Plane  ", &cam->m_farPlane);
			ImGui::DragFloat("Width", &cam->m_viewWidth);
			ImGui::DragFloat("Hight", &cam->m_viewHeight);
			ImGui::DragFloat("Camera Speed", m_editorCameraRig->SpeedPtr(), 0.1f, 0.f, 200.f);
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("RenderStatistics"))
		{
			ImGui::Text("Render Statistics");
			ImGui::Separator();
			ImGui::Text("FPS: %d", Time->GetFramesPerSecond());
			ImGui::Text("Screen Size: %u x %u", ScreenResizeBus::Get().GetWidth(), ScreenResizeBus::Get().GetHeight());
			// ★ DX11 드로우콜 카운터를 걷었다 (D4). DX11 드로우가 사라진 뒤로
			//   이 값은 늘 0이었다 - 0은 "안 그렸다"와 "셀 수 없다"를 구분해
			//   주지 않는다. DX12 패스별 통계는 Settings > Pipeline Setting에 있다.
			ImGui::Separator();
			ImGui::Text("ShadowMapPass: %.5f ms", RenderStatistics->GetRenderState("ShadowMapPass"));
			ImGui::Text("GBufferPass: %.5f ms", RenderStatistics->GetRenderState("GBufferPass"));
			ImGui::Text("SSAOPass: %.5f ms", RenderStatistics->GetRenderState("SSAOPass"));
			ImGui::Text("DeferredPass: %.5f ms", RenderStatistics->GetRenderState("DeferredPass"));
			ImGui::Text("SSGIPass: %.5f ms", RenderStatistics->GetRenderState("SSGIPass"));
			ImGui::Text("ForwardPass: %.5f ms", RenderStatistics->GetRenderState("ForwardPass"));
			ImGui::Text("LightMapPass: %.5f ms", RenderStatistics->GetRenderState("LightMapPass"));
			ImGui::Text("WireFramePass: %.5f ms", RenderStatistics->GetRenderState("WireFramePass"));
			ImGui::Text("SkyBoxPass: %.5f ms", RenderStatistics->GetRenderState("SkyBoxPass"));
			ImGui::Text("BloomPass: %.5f ms", RenderStatistics->GetRenderState("PostProcessPass"));
			ImGui::Text("AAPass: %.5f ms", RenderStatistics->GetRenderState("AAPass"));
			ImGui::Text("ToneMapPass: %.5f ms", RenderStatistics->GetRenderState("ToneMapPass"));
			ImGui::Text("SpritePass: %.5f ms", RenderStatistics->GetRenderState("SpritePass"));
			ImGui::Text("UIPass: %.5f ms", RenderStatistics->GetRenderState("UIPass"));
			ImGui::Text("BlitPass: %.5f ms", RenderStatistics->GetRenderState("BlitPass"));

			ImGui::Text("SSR: %.5f ms", RenderStatistics->GetRenderState("ScreenSpaceReflectionPass"));
			ImGui::Text("SSS: %.5f ms", RenderStatistics->GetRenderState("SubsurfaceScatteringPass"));
			ImGui::Text("Vignette: %.5f ms", RenderStatistics->GetRenderState("VignettePass"));
			ImGui::Text("ColorGrading: %.5f ms", RenderStatistics->GetRenderState("ColorGradingPass"));
			ImGui::Text("VolumetricFog: %.5f ms", RenderStatistics->GetRenderState("VolumetricFogPass"));
			ImGui::EndPopup();
		}
		ImGui::PopFont();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
	else
	{
		ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	}

    if (obj && !selectMode)
    {
		auto scene = SceneManagers->GetActiveScene();
		auto& selectedObjects = scene->m_selectedEntities;

		if (auto* rect = obj->GetComponent<RectTransformComponent>())
		{
			static std::unordered_map<Entity*, math::vector2> startWorldPivots;
            static std::vector<EditorObjectOperations::PropertyEdit> edits;
			static math::vector2 startWorldPos{};

			bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
			bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
			bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

			if (isWindowHovered && !isDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				startWorldPivots.clear(); edits.clear();
				for (auto* target : selectedObjects)
				{
					if (auto* rt = target->GetComponent<RectTransformComponent>())
                    {
                        startWorldPivots[target] = rt->GetWorldPivotPosition();
                        edits.push_back(EditorObjectOperations::CapturePropertyEdit(*rt, {"m_anchoredPosition"}));
                    }
				}
				auto world = rect->GetWorldRect();
				startWorldPos = { world.x + world.width * rect->GetPivot().x,
								  world.y + world.height * rect->GetPivot().y };
			}

			math::matrix4x4 deltaMat = math::matrix4x4::identity();
			ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix,
				&deltaMat.m[0][0], useSnap ? &snap[0] : nullptr,
				boundSizing ? bounds : nullptr,
				boundSizingSnap ? boundsSnap : nullptr);

			const bool matrixChanged =
				!(deltaMat == math::matrix4x4::identity());

			if (matrixChanged)
			{
				math::matrix4x4 manipulatedWorld{};
				std::memcpy(
					&manipulatedWorld.m[0][0], matrix, sizeof(manipulatedWorld));
				math::vector2 newWorldPos{
					manipulatedWorld.m[3][0], manipulatedWorld.m[3][1] };
				math::vector2 offset = newWorldPos - startWorldPos;
				if (offset.x != 0.f || offset.y != 0.f)
				{
					for (auto* target : selectedObjects)
					{
						auto it = startWorldPivots.find(target);
						if (it == startWorldPivots.end()) continue;
						if (auto* rt = target->GetComponent<RectTransformComponent>())
						{
							rt->SetWorldPivotPosition(it->second + offset);
							// 부모 rect를 여기서 직접 만들지 않는다 — (0,0,W,H)로 적혀 있어 캔버스
							// 규약과 (W/2,H/2)만큼 어긋났고, 자식으로 전파도 되지 않아 부모를 끌면
							// 자식이 따라오지 않았다. 순회는 드라이버가 맡는다(PHASE 7-5).
							if (Scene* scene = SceneManagers->GetActiveScene())
								scene->LayoutUISubtree(target);
						}
					}
				}
			}

            if (mouseReleased && !edits.empty())
                EditorObjectOperations::CommitPropertyEdits(std::move(edits));

		}
		else
		{
			static std::unordered_map<Entity*, math::matrix4x4> startWorldMatrices;
            static std::vector<EditorObjectOperations::PropertyEdit> edits;

			bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
			bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
			bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

			if (isWindowHovered && !isDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				startWorldMatrices.clear(); edits.clear();
				for (auto* target : selectedObjects)
				{
					startWorldMatrices[target] =
						target->Transform_().GetWorldMatrix();
                    edits.push_back(EditorObjectOperations::CapturePropertyEdit(target->Transform_(), {"position", "rotation", "scale"}));
				}
			}

			math::matrix4x4 deltaMat = math::matrix4x4::identity();
			ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix,
				&deltaMat.m[0][0], useSnap ? &snap[0] : nullptr,
				boundSizing ? bounds : nullptr,
				boundSizingSnap ? boundsSnap : nullptr);

			math::matrix4x4 manipulatedWorld{};
			std::memcpy(&manipulatedWorld.m[0][0], matrix, sizeof(manipulatedWorld));
			const math::matrix4x4 parentWorldInverse =
				math::inverse(ResolveParentWorldMatrix(obj));
			const math::matrix4x4 newLocalMatrix =
				manipulatedWorld * parentWorldInverse;

			if (!(deltaMat == math::matrix4x4::identity()))
			{
			obj->Transform_().SetLocalMatrix(
				newLocalMatrix, TransformWriteReason::Gizmo);
				const math::matrix4x4 newWorld =
					obj->Transform_().GetWorldMatrix();
				auto itSelf = startWorldMatrices.find(obj);
				if (itSelf != startWorldMatrices.end())
				{
					const math::vector3 offset =
						newWorld.translation() - itSelf->second.translation();

					if (math::length_sq(offset) > 0.f &&
						mCurrentGizmoOperation == ImGuizmo::TRANSLATE)
					{
						for (auto* target : selectedObjects)
						{
							if (target == obj) continue;
							auto itStart = startWorldMatrices.find(target);
							if (itStart == startWorldMatrices.end()) continue;
							const math::matrix4x4 targetWorld = itStart->second *
								math::translation_matrix(offset);
							const math::matrix4x4 targetLocal = targetWorld *
								math::inverse(ResolveParentWorldMatrix(target));
			target->Transform_().SetLocalMatrix(
				targetLocal, TransformWriteReason::Gizmo);
						}
					}
				}
			}

            if (mouseReleased && !edits.empty())
                EditorObjectOperations::CommitPropertyEdits(std::move(edits));

		}
    }

	ImGuizmo::ViewManipulate(cameraView, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop + 16), ImVec2(128, 128), IM_COL32(0, 0, 0, 0));

	{
		auto scene = SceneManagers->GetActiveScene();
        auto& selectedObjects = scene->m_selectedEntities;
		// 기즈모로 변환된 카메라 위치, 회전 적용
		math::matrix4x4 viewMatrix{};
		std::memcpy(&viewMatrix.m[0][0], cameraView, sizeof(viewMatrix));
		if (const auto cameraTransform = math::decompose(math::inverse(viewMatrix)))
		{
			cam->m_eyePosition = cameraTransform->translation;
			cam->rotate = math::normalize(cameraTransform->rotation);
			cam->m_forward = math::normalize(math::rotate(cam->FORWARD, cam->rotate));
			cam->m_up = math::normalize(math::rotate(cam->UP, cam->rotate));
			cam->m_right = math::normalize(math::rotate(cam->RIGHT, cam->rotate));
		}
	}

	if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		m_editorCameraRig->HandleMovement(Time->GetElapsedSeconds());
	}

	if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
		auto scene = SceneManagers->GetActiveScene();
		auto selectedObjects = scene->m_selectedEntities;
        std::vector<EditorObjectOperations::PropertyEdit> edits;
        for (auto* target : selectedObjects)
        {
            edits.push_back(EditorObjectOperations::CapturePropertyEdit(target->Transform_(), {"position", "rotation"}));
            target->Transform_().SetWorldRotation(cam->rotate, TransformWriteReason::Gizmo);
            target->Transform_().SetWorldPosition(cam->m_eyePosition, TransformWriteReason::Gizmo);
        }
        EditorObjectOperations::CommitPropertyEdits(std::move(edits));
	}
	else if (ImGui::IsWindowFocused() && ImGui::IsKeyDown(ImGuiKey_F)) {
		auto scene = SceneManagers->GetActiveScene();
		auto selectedObjects = scene->m_selectedEntities;
		for (auto* target : selectedObjects)
		{
			cam->MoveToTarget(
				target->Transform_().GetWorldPosition() - cam->m_forward * 5.f);
			break;
		}
	}

	auto scene = SceneManagers->GetActiveScene();
	auto& sceneSelectedObj = scene->m_selectedEntity;
	auto& selectedObjects = scene->m_selectedEntities;
	static bool useGizmo = false;
	static float gizmoTimer = 0.f;

	if (ImGuizmo::IsUsing())
	{
		useGizmo = true;
		if (useWindow)
		{
			ImGui::End();
			ImGui::PopStyleColor(2);
		}
		return;
	}

	if (useGizmo)
	{
		gizmoTimer += Time->GetElapsedSeconds();
		if (gizmoTimer > 0.5f)
		{
			useGizmo = false;
			gizmoTimer = 0.f;
		}
	}
	else
	{
		gizmoTimer = 0.f;
	}

	
	TerrainBrush* editorTerrainBrush = EditorSessionState::Get().FindTerrainBrush();
	if(nullptr == editorTerrainBrush || false == editorTerrainBrush->m_isEditMode)
	{
		if (!useGizmo &&
			ImGui::IsWindowHovered() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			float closest = FLT_MAX;
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 imagePos = imageMin; // 이미지 좌상단 위치
			ImVec2 imageSize = imageMax;

			Ray ray = CreateRayFromCamera(cam, mousePos, imagePos, imageSize);

			const auto& sceneObjects = SceneManagers->GetActiveScene()->m_Entities;
			auto hits = PickObjectsFromRay(ray, sceneObjects);

			if (!hits.empty())
			{
				m_hitResults = hits;

				m_currentHitIndex = m_currentHitIndex % m_hitResults.size();
				Entity* selected = m_hitResults[m_currentHitIndex].object;
				m_currentHitIndex++;

				bool shift = ImGui::GetIO().KeyShift;
                auto desired = selectedObjects;
                if (shift)
                {
                    auto it = std::find(desired.begin(), desired.end(), selected);
                    if (it != desired.end()) desired.erase(it); else desired.push_back(selected);
                }
                else desired = {selected};
                std::vector<EntityHandle> handles;
                for (auto* target : desired) handles.push_back(scene->HandleOf(target->m_index));
                EditorObjectOperations::Select(scene, handles);
			}
			else
			{
				m_hitResults.clear();
				m_currentHitIndex = 0;
			}
		}

		ImRect dropRect = ImRect(imageMin, imageMax);
		static file::path previewModelPath;
		static Entity* dragPreviewObject = nullptr;
		static ImGuiPayload* dragPayload = nullptr;

		if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("MyDropTarget")))
		{
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Model", ImGuiDragDropFlags_AcceptBeforeDelivery);
			if (!dragPayload || dragPayload != payload)
			{
				dragPayload = const_cast<ImGuiPayload*>(payload);
				if (previewModelPath.empty() && !dragPreviewObject && dragPayload)
				{
					const char* droppedFilePath = static_cast<const char*>(dragPayload->Data);
					file::path filename = file::path(droppedFilePath).filename();
					previewModelPath = PathFinder::Relative("Models\\") / filename;

					Entity* createdObj = nullptr;
					Meta::UndoManager::GetInstance()->Execute(
						std::make_unique<Meta::LoadModelToSceneObjCommand>(
							scene,
							DataSystems->LoadModelAssetGenerationByPath(previewModelPath.string()),
							&createdObj));
					dragPreviewObject = createdObj;
				}
			}
			else
			{
				ImVec2 mousePos = ImGui::GetMousePos();
				Ray ray = CreateRayFromCamera(cam, mousePos, imageMin, imageMax);

				float distance;
				if (RayIntersectsPlane(ray, { 0, 1, 0 }, { 0, 0, 0 }, distance))
				{
					const math::vector3 worldPos = ray.origin + ray.direction * distance;

					if (payload->IsPreview() && dragPreviewObject)
					{
		dragPreviewObject->Transform_().SetPosition(
			worldPos, TransformWriteReason::Gizmo);
					}
				}

				if (!dragPayload->IsPreview() && dragPreviewObject)
				{
					dragPreviewObject = nullptr;
					dragPayload = nullptr;
					previewModelPath.clear();
				}
			}

			if (const ImGuiPayload* HDRPayload = ImGui::AcceptDragDropPayload("HDR"))
			{
				const char* droppedFilePath = (const char*)HDRPayload->Data;
				file::path filename = droppedFilePath;
				file::path filepath = PathFinder::Relative("HDR\\") / filename.filename();
				RuntimeSettings::Get().SetSkyboxTextureName(filepath.string());
				std::string skyError;
				if (!EnhancedSceneRenderer::SetSkyBoxPath(filepath.string(), skyError))
				{
					Debug->LogError("SkyBox 변경 실패: " + skyError);
				}
			}

			if (const ImGuiPayload* prefabPayload = ImGui::AcceptDragDropPayload("Prefab"))
			{
				const char* droppedFilePath = (const char*)prefabPayload->Data;
				file::path filename = droppedFilePath;
				file::path filepath = PathFinder::Relative("Prefabs\\") / filename.filename();
				auto prefab = PrefabUtilitys->LoadPrefabFullPath(filepath.string().c_str());
				if (prefab)
				{
					EditorObjectOperations::InstantiatePrefab(prefab, filename.stem().string());
				}
			}

			ImGui::EndDragDropTarget();
		}

	}
	//====================
	// 선택 아이템 있을시 처리
	static TerrainComponent* prevTerrain = nullptr;
	if (sceneSelectedObj && sceneSelectedObj->HasComponent<TerrainComponent>())
	{
		if (editorTerrainBrush == nullptr)
		{
			editorTerrainBrush = &EditorSessionState::Get().GetOrCreateTerrainBrush();
		}

		TerrainComponent* terrainComponent = sceneSelectedObj->GetComponent<TerrainComponent>();
		if (terrainComponent)
		{
			if (editorTerrainBrush->m_isEditMode)
			{
				terrainComponent->SetTerrainBrush(editorTerrainBrush);
				if (ImGui::IsWindowHovered())
				{
					ImVec2 mousePos = ImGui::GetMousePos();
					Ray ray = CreateRayFromCamera(cam, mousePos, imageMin, imageMax);
					//    TerrainComponent 내부에서는 Y=0 평면 위에 heightMap이 있다고 가정
					const math::vector3 origin = ray.origin;
					const math::vector3 direction = ray.direction;
					// 절대로 방향 벡터의 y 성분이 0이면 나눌 수 없으므로 먼저 체크
					if (direction.y < 0.0f)
					{
						// t 계산: Y=0 평면 얻기
						float t = -origin.y / direction.y;
						if (t >= 0.0f)
						{
							// 충돌 지점 P = origin + t * direction
							math::vector3 hitPos{};
							hitPos.x = origin.x + t * direction.x;
							hitPos.y = 0.0f; // 당연히 y=0
							hitPos.z = origin.z + t * direction.z;

							// 4) 충돌 지점(P)의 XZ → HeightMap 인덱스(격자) 변환
							//    TerrainComponent의 m_width, m_height, m_gridSize가 필요
							float gridSize = 1.0f; // 예: 1.0f, 2.0f 등
							int   tileX = static_cast<int>(floorf(hitPos.x / gridSize));
							int   tileY = static_cast<int>(floorf(hitPos.z / gridSize));

							editorTerrainBrush->m_center = { static_cast<float>(tileX), static_cast<float>(tileY) };

							if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
							{
								if (editorTerrainBrush->m_mode == TerrainBrush::Mode::FoliageMode)
								{
									FoliageComponent* foliage = sceneSelectedObj->GetComponent<FoliageComponent>();
									if (foliage)
									{
										if (editorTerrainBrush->m_foliageMode == TerrainBrush::FoliageMode::Paint)
										{
											foliage->AddRandomInstancesInBrush(terrainComponent, *editorTerrainBrush, editorTerrainBrush->m_foliageTypeID, editorTerrainBrush->m_foliageDensity);
										}
										else
										{
											foliage->RemoveInstancesInBrush(terrainComponent, *editorTerrainBrush);
										}

										auto renderScene = SceneManagers->GetRenderScene();
										if (renderScene) renderScene->UpdateCommand(foliage);
									}
								}
								else
								{
									terrainComponent->ApplyBrush(*editorTerrainBrush);
								}
							}
						}
					}
				}
			}
			else
			{
				terrainComponent->SetTerrainBrush(nullptr);
			}
			prevTerrain = terrainComponent;
		}
	}
	else if (prevTerrain)
	{
		prevTerrain->SetTerrainBrush(nullptr);
		prevTerrain = nullptr;
	}

	//=========================

	if (useWindow)
	{
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

math::vector3 SceneViewWindow::ConvertMouseToWorldPosition(Camera* cam, const ImVec2& mouseScreenPos, const ImVec2& imagePos, const ImVec2& imageSize, float depth)
{
	const float normX = (mouseScreenPos.x - imagePos.x) / imageSize.x;
	const float normY = (mouseScreenPos.y - imagePos.y) / imageSize.y;

	const float ndcX = normX * 2.0f - 1.0f;
	const float ndcY = (1.0f - normY) * 2.0f - 1.0f;
	const math::vector4 clipPosition{ ndcX, ndcY, depth, 1.0f };
	const math::matrix4x4 inverseViewProjection =
		math::inverse(cam->CalculateView() * cam->CalculateProjection());
	const math::vector4 worldPosition = clipPosition * inverseViewProjection;

	if (std::fabs(worldPosition.w) <= 1.0e-6f)
	{
		return { worldPosition.x, worldPosition.y, worldPosition.z };
	}
	const float inverseW = 1.0f / worldPosition.w;
	return {
		worldPosition.x * inverseW,
		worldPosition.y * inverseW,
		worldPosition.z * inverseW };
}

Ray SceneViewWindow::CreateRayFromCamera(Camera* cam, const ImVec2& mousePos, const ImVec2& imagePos, const ImVec2& imageSize)
{
	const math::vector3 nearPoint = ConvertMouseToWorldPosition(
		cam, mousePos, imagePos, imageSize, 0.0f);
	const math::vector3 farPoint = ConvertMouseToWorldPosition(
		cam, mousePos, imagePos, imageSize, 1.0f);
	return Ray{ nearPoint, math::normalize(farPoint - nearPoint) };
}

Entity* SceneViewWindow::PickObjectFromRay(const Ray& ray, const std::vector<std::unique_ptr<Entity>>& sceneObjects)
{
	Entity* selected = nullptr;
	float closestDistance = FLT_MAX;
	const math::ray pickRay{ ray.origin, ray.direction };

	for (auto& obj : sceneObjects)
	{
		auto* meshComp = obj->GetComponent<MeshRenderer>();
		// I5-D5b — "그릴 메시가 있는가"는 창구가 판정한다. legacy m_Mesh를
		// 직접 가드로 쓰면 D4f의 은퇴가 이 조건을 통째로 거짓으로 만들어
		// 피킹이 조용히 죽는다(선택 불가는 렌더 회귀로 안 잡힌다).
		if (!meshComp || !meshComp->HasRenderableMesh())
			continue;

		const math::aabb worldAABB = meshComp->GetBoundingBox();
		if (worldAABB.is_empty()) continue;

		float hitDistance;
		if (math::raycast(pickRay, worldAABB, hitDistance))
		{
			if (hitDistance < closestDistance)
			{
				closestDistance = hitDistance;
				selected = obj.get();
			}
		}

	}

	return selected;
}

std::vector<RayHitResult> SceneViewWindow::PickObjectsFromRay(const Ray& ray, const std::vector<std::unique_ptr<Entity>>& sceneObjects)
{
	std::vector<RayHitResult> hits;
	const math::ray pickRay{ ray.origin, ray.direction };

	for (auto& obj : sceneObjects)
	{
		auto* meshComp = obj->GetComponent<MeshRenderer>();
		auto* cameraComp = obj->GetComponent<CameraComponent>();
		auto* lightComp = obj->GetComponent<LightComponent>();
		if (meshComp && meshComp->HasRenderableMesh()) // I5-D5b — 위와 같은 창구
		{
			const math::aabb worldAABB = meshComp->GetBoundingBox();
			if (worldAABB.is_empty()) continue;

			float hitDistance;
			if (math::raycast(pickRay, worldAABB, hitDistance))
			{
				hits.push_back({ obj.get(), hitDistance });
			}
		}
		else if (cameraComp)
		{
			const math::aabb worldAABB = cameraComp->GetEditorBoundingBox();

			float hitDistance;
			if (math::raycast(pickRay, worldAABB, hitDistance))
			{
				hits.push_back({ obj.get(), hitDistance });
			}
		}
		else if (lightComp)
		{
			const math::aabb worldAABB = lightComp->GetEditorBoundingBox();

			float hitDistance;
			if (math::raycast(pickRay, worldAABB, hitDistance))
			{
				hits.push_back({ obj.get(), hitDistance });
			}
		}
	}

	// 거리순 정렬 (가까운 오브젝트가 먼저)
	std::sort(hits.begin(), hits.end(), [](const RayHitResult& a, const RayHitResult& b) 
	{
		return a.distance < b.distance;
	});

	return hits;
}
