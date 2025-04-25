#include "SceneViewWindow.h"
#include "SceneRenderer.h"
#include "ImGuizmo.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "Scene.h"
#include "Camera.h"
#include "GameObject.h"
#include "DataSystem.h"
#include "RenderState.h"

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

SceneViewWindow::SceneViewWindow(SceneRenderer* ptr) : m_sceneRenderer(ptr)
{
}

void SceneViewWindow::RenderSceneViewWindow()
{
	auto obj = m_sceneRenderer->m_renderScene->GetSelectSceneObject();
	if (obj)
	{
		auto mat = obj->m_transform.GetWorldMatrix();
		XMFLOAT4X4 objMat;
		XMStoreFloat4x4(&objMat, mat);
		auto view = m_sceneRenderer->m_pEditorCamera->CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		auto proj = m_sceneRenderer->m_pEditorCamera->CalculateProjection();
		XMFLOAT4X4 projMatrix;
		XMStoreFloat4x4(&projMatrix, proj);

		RenderSceneView(&floatMatrix.m[0][0], &projMatrix.m[0][0], &objMat.m[0][0], true, obj, m_sceneRenderer->m_pEditorCamera.get());

	}
	else
	{
		auto view = m_sceneRenderer->m_pEditorCamera->CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		auto proj = m_sceneRenderer->m_pEditorCamera->CalculateProjection();
		XMFLOAT4X4 projMatrix;
		XMStoreFloat4x4(&projMatrix, proj);
		XMFLOAT4X4 identityMatrix;
		XMStoreFloat4x4(&identityMatrix, XMMatrixIdentity());

		RenderSceneView(&floatMatrix.m[0][0], &projMatrix.m[0][0], &identityMatrix.m[0][0], false, nullptr, m_sceneRenderer->m_pEditorCamera.get());
	}
}

void SceneViewWindow::RenderSceneView(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, GameObject* obj, Camera* cam)
{
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);
	static bool useSnap = false;
	static float snap[3] = { 1.f, 1.f, 1.f };
	static float bounds[] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
	static float boundsSnap[] = { 0.1f, 0.1f, 0.1f };
	static bool boundSizing = false;
	static bool boundSizingSnap = false;
	static bool selectMode = false;
	static enum class SelectGuizmoMode selectGizmoMode = SelectGuizmoMode::Translate;
	static const char* buttons[] = {
		ICON_FA_EYE,
		ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT,
		ICON_FA_ARROWS_ROTATE,
		ICON_FA_GROUP_ARROWS_ROTATE
	};
	static const int buttonCount = sizeof(buttons) / sizeof(buttons[0]);

	ImGuizmo::SetOrthographic(m_sceneRenderer->m_pEditorCamera->m_isOrthographic);
	ImGuizmo::BeginFrame();

	if (ImGui::IsKeyPressed(ImGuiKey_T))
		selectGizmoMode = SelectGuizmoMode::Translate;
	if (ImGui::IsKeyPressed(ImGuiKey_R))
		selectGizmoMode = SelectGuizmoMode::Rotate;
	if (ImGui::IsKeyPressed(ImGuiKey_G)) // r Key
		selectGizmoMode = SelectGuizmoMode::Scale;
	if (ImGui::IsKeyPressed(ImGuiKey_F))
		useSnap = !useSnap;
	if (ImGui::IsKeyPressed(ImGuiKey_V))
		selectGizmoMode = SelectGuizmoMode::Select;

	ImGuiIO& io = ImGui::GetIO();
	float viewManipulateRight = io.DisplaySize.x;
	float viewManipulateTop = 0;
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

		float windowWidth = (float)ImGui::GetWindowWidth();
		float windowHeight = (float)ImGui::GetWindowHeight();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
		viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
		viewManipulateTop = ImGui::GetWindowPos().y;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		gizmoWindowFlags |= ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;

		float x = windowWidth;//window->InnerRect.Max.x - window->InnerRect.Min.x;
		float y = windowHeight;//window->InnerRect.Max.y - window->InnerRect.Min.y;

		/*m_sceneRenderer->m_pEditorCamera->m_viewWidth = windowWidth;
		m_sceneRenderer->m_pEditorCamera->m_viewHeight = windowHeight;
		auto view = m_sceneRenderer->m_pEditorCamera->CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		cameraView = &floatMatrix.m[0][0];*/

		ImGui::Image((ImTextureID)cam->m_renderTarget->m_pSRV, ImVec2(x, y));

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
			m_sceneRenderer->m_bShowGridSettings = true;
		}

		ImGui::SameLine();

		currentPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 5, currentPos.y));

		if (ImGui::Button(m_sceneRenderer->m_pEditorCamera->m_isOrthographic ? ICON_FA_EYE_LOW_VISION " Orthographic" : ICON_FA_ARROWS_TO_EYE " Perspective"))
		{
			m_sceneRenderer->m_pEditorCamera->m_isOrthographic = !m_sceneRenderer->m_pEditorCamera->m_isOrthographic;
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
		ImGui::SetCursorScreenPos(ImVec2(windowWidth - 250.f, currentPos.y));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
		for (int i = 0; i < buttonCount; i++)
		{
			// 선택된 버튼은 활성화 색상, 나머지는 비활성화 색상으로 설정합니다.
			if (i == (int)selectGizmoMode)
			{
				// 활성화된 버튼 색상 (예: 녹색 계열)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.1f, 0.9f, 0.8f));
			}
			else
			{
				// 비활성화된 버튼 색상 (예: 회색 계열)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
			}

			// 버튼 렌더링: 버튼을 클릭하면 선택된 인덱스를 업데이트합니다.
			if (ImGui::Button(buttons[i]))
			{
				selectGizmoMode = (SelectGuizmoMode)i;
			}

			ImGui::SameLine();
			currentPos = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(currentPos.x + 1, currentPos.y));

			// PushStyleColor 호출마다 PopStyleColor를 호출해 원래 상태로 복원합니다.
			ImGui::PopStyleColor();
		}
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
		ImGui::PushFont(DataSystems->GetSmallFont());
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
			ImGui::InputFloat("Width", &cam->m_viewWidth);
			ImGui::InputFloat("Hight", &cam->m_viewHeight);
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("RenderStatistics"))
		{
			ImGui::Text("Render Statistics");
			ImGui::Separator();
			ImGui::Text("FPS: %d", Time->GetFramesPerSecond());
			ImGui::Text("Screen Size: %d x %d", (int)DeviceState::g_ClientRect.width, (int)DeviceState::g_ClientRect.height);
			//Draw Call Count
			ImGui::Text("Draw Call Count: %d", DirectX11::GetDrawCallCount());
			ImGui::Separator();
			ImGui::Text("ShadowMapPass: %.5f ms", RenderStatistics->GetRenderState("ShadowMapPass"));
			ImGui::Text("GBufferPass: %.5f ms", RenderStatistics->GetRenderState("GBufferPass"));
			ImGui::Text("SSAOPass: %.5f ms", RenderStatistics->GetRenderState("SSAOPass"));
			ImGui::Text("DeferredPass: %.5f ms", RenderStatistics->GetRenderState("DeferredPass"));
			ImGui::Text("ForwardPass: %.5f ms", RenderStatistics->GetRenderState("ForwardPass"));
			ImGui::Text("LightMapPass: %.5f ms", RenderStatistics->GetRenderState("LightMapPass"));
			ImGui::Text("WireFramePass: %.5f ms", RenderStatistics->GetRenderState("WireFramePass"));
			ImGui::Text("SkyBoxPass: %.5f ms", RenderStatistics->GetRenderState("SkyBoxPass"));
			ImGui::Text("PostProcessPass: %.5f ms", RenderStatistics->GetRenderState("PostProcessPass"));
			ImGui::Text("AAPass: %.5f ms", RenderStatistics->GetRenderState("AAPass"));
			ImGui::Text("ToneMapPass: %.5f ms", RenderStatistics->GetRenderState("ToneMapPass"));
			ImGui::Text("GridPass: %.5f ms", RenderStatistics->GetRenderState("GridPass"));
			ImGui::Text("SpritePass: %.5f ms", RenderStatistics->GetRenderState("SpritePass"));
			ImGui::Text("UIPass: %.5f ms", RenderStatistics->GetRenderState("UIPass"));
			ImGui::Text("BlitPass: %.5f ms", RenderStatistics->GetRenderState("BlitPass"));
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
	static XMMATRIX oldLocalMatrix{};
	static bool wasDragging = false;

	bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
	bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

	if (isWindowHovered && !isDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		oldLocalMatrix = obj->m_transform.GetLocalMatrix();
	}

	ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix,
		nullptr, useSnap ? &snap[0] : nullptr, boundSizing ? bounds : nullptr, boundSizingSnap ? boundsSnap : nullptr);

	XMMATRIX parentMat = SceneManagers->GetActiveScene()->GetGameObject(obj->m_parentIndex)->m_transform.GetWorldMatrix();
	XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentMat);
	XMMATRIX newLocalMatrix = XMMatrixMultiply(XMMATRIX(matrix), parentWorldInverse);

	bool matrixChanged = (Mathf::Matrix(oldLocalMatrix) != newLocalMatrix);

	if (wasDragging && mouseReleased && matrixChanged)
	{
		Meta::MakeCustomChangeCommand(
			[=] 
			{ 
				XMMATRIX copy = oldLocalMatrix;
				obj->m_transform.SetLocalMatrix(copy);
			},
			[=] 
			{ 
				XMMATRIX copy = newLocalMatrix;
				obj->m_transform.SetLocalMatrix(copy);
			}
		);
	}

	obj->m_transform.SetLocalMatrix(newLocalMatrix);
	wasDragging = isDragging;
}

	ImGuizmo::ViewManipulate(cameraView, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop + 30), ImVec2(128, 128), 0x10101010);

	{
		// 기즈모로 변환된 카메라 위치, 회전 적용
		XMVECTOR poss;
		XMVECTOR rots;
		XMVECTOR scales;
		XMMatrixDecompose(&scales, &rots, &poss, XMMatrixInverse(nullptr, XMMATRIX(cameraView)));
		cam->m_eyePosition = poss;
		cam->m_rotation = rots;

		XMVECTOR rotDir = XMVector3Rotate(cam->FORWARD, rots);

		cam->m_forward = rotDir;
	}

	if (useWindow)
	{
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}
