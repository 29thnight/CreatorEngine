#ifndef DYNAMICCPP_EXPORTS
#include "SceneViewWindow.h"
#include "SceneRenderer.h"
#include "GizmoRenderer.h"
#include "ImGuizmo.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "Scene.h"
#include "Camera.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "GameObject.h"
#include "DataSystem.h"
#include "RenderState.h"
#include "Terrain.h"

bool useWindow = true;
bool editWindow = true;
int gizmoCount = 1;
float camDistance = 8.f;
static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
TerrainBrush* terrainBrush = nullptr;

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

bool RayIntersectsPlane(const Ray& ray, const Mathf::Vector3& planeNormal, const Mathf::Vector3& planePoint, float& outDistance)
{
	float denom{};
    denom = planeNormal.Dot(ray.direction);
	// 노멀과 평행하면 교차 없음
	if (fabs(denom) < 1e-6f)
		return false;

	Mathf::Vector3 diff = planePoint - ray.origin;
	float t = diff.Dot(planeNormal) / denom;

	if (t < 0)
		return false;

	outDistance = t;
	return true;
}

SceneViewWindow::SceneViewWindow(SceneRenderer* ptr, GizmoRenderer* gizmo_ptr) : 
	m_sceneRenderer(ptr),
	m_gizmoRenderer(gizmo_ptr)
{
}

void SceneViewWindow::RenderSceneViewWindow()
{
	auto scene = SceneManagers->GetActiveScene();
	auto obj = scene->GetSelectSceneObject();
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
	static GameObject* selected = nullptr;
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

		auto scene = SceneManagers->m_ActiveRenderScene;

		auto renderData = RenderPassData::GetData(cam);

		ImGui::Image((ImTextureID)renderData->m_renderTarget->m_pSRV, ImVec2(x, y));
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
	
		XMMATRIX parentMat = GameObject::FindIndex(obj->m_parentIndex)->m_transform.GetWorldMatrix();
		XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentMat);
		XMMATRIX newLocalMatrix = XMMatrixMultiply(XMMATRIX(matrix), parentWorldInverse);
	
		bool matrixChanged = (Mathf::Matrix(oldLocalMatrix) != newLocalMatrix);
		//Undo Redo 커멘드를 저장할 목적의 코드
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
		//실시간 변화
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

	if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		cam->HandleMovement(Time->GetElapsedSeconds());
	}

	auto scene = SceneManagers->GetActiveScene();
	auto& sceneSelectedObj = scene->m_selectedSceneObject;
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

	if (!useGizmo && ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		float closest = FLT_MAX;
		ImVec2 mousePos = ImGui::GetMousePos();
		ImVec2 imagePos = imageMin; // 이미지 좌상단 위치
		ImVec2 imageSize = imageMax;

		Ray ray = CreateRayFromCamera(cam, mousePos, imagePos, imageSize);

		auto sceneObjects = SceneManagers->GetActiveScene()->m_SceneObjects;
		auto hits = PickObjectsFromRay(ray, sceneObjects);

		if (!hits.empty())
		{
			// 이전과 동일한 히트 목록이면 인덱스 증가
			if (hits.size() == m_hitResults.size())
			{
				bool allSame = true;
				for (size_t i = 0; i < hits.size(); ++i)
				{
					if (hits[i].object != m_hitResults[i].object)
					{
						allSame = false;
						break;
					}
				}

				if (allSame)
					m_currentHitIndex = (m_currentHitIndex + 1) % hits.size();
				else
					m_currentHitIndex = 0;
			}
			else
			{
				m_currentHitIndex = 0;
			}

			m_hitResults = hits;

			GameObject* selected = m_hitResults[m_currentHitIndex].object;
			sceneSelectedObj = selected;
		}
		else
		{
			m_hitResults.clear();
			m_currentHitIndex = 0;
		}
	}

	ImRect dropRect = ImRect(imageMin, imageMax);
	static file::path previewModelPath;
	static GameObject* dragPreviewObject = nullptr;
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

				dragPreviewObject = Model::LoadModelToSceneObj(
					DataSystems->LoadCashedModel(previewModelPath.string()),
					*scene);
			}
		}
		else
		{
			ImVec2 mousePos = ImGui::GetMousePos();
			Ray ray = CreateRayFromCamera(cam, mousePos, imageMin, imageMax);

			float distance;
			if (RayIntersectsPlane(ray, { 0, 1, 0 }, { 0, 0, 0 }, distance))
			{
				Mathf::Vector3 worldPos = ray.origin + Mathf::Vector3(ray.direction) * distance;

				if (payload->IsPreview() && dragPreviewObject)
				{
					dragPreviewObject->m_transform.SetPosition(worldPos);
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
			m_sceneRenderer->ApplyNewCubeMap(filepath.string());
		}

		ImGui::EndDragDropTarget(); 
	}
	
	//====================
	// 선택 아이템 있을시 처리
	if (sceneSelectedObj != nullptr) {

		//터레인 일때	
		if (sceneSelectedObj->HasComponent<TerrainComponent>()) {
			if (terrainBrush == nullptr) {
				terrainBrush = new TerrainBrush();
			}

			TerrainComponent* terrainComponent = sceneSelectedObj->GetComponent<TerrainComponent>();
			if (terrainComponent != nullptr) {
				terrainComponent->SetTerrainBrush(terrainBrush);
				if (ImGui::IsWindowHovered()) {
					ImVec2 mousePos = ImGui::GetMousePos();
					Ray ray = CreateRayFromCamera(cam, mousePos, imageMin, imageMax);

					//    TerrainComponent 내부에서는 Y=0 평면 위에 heightMap이 있다고 가정
					XMFLOAT3 origin = ray.origin;
					XMFLOAT3 direction = ray.direction;

					// 절대로 방향 벡터의 y 성분이 0이면 나눌 수 없으므로 먼저 체크
					if (direction.y < 0.0f)
					{
						// t 계산: Y=0 평면 얻기
						float t = -origin.y / direction.y;
						if (t >= 0.0f)
						{
							// 충돌 지점 P = origin + t * direction
							XMFLOAT3 hitPos;
							hitPos.x = origin.x + t * direction.x;
							hitPos.y = 0.0f; // 당연히 y=0
							hitPos.z = origin.z + t * direction.z;

							// 4) 충돌 지점(P)의 XZ → HeightMap 인덱스(격자) 변환
							//    TerrainComponent의 m_width, m_height, m_gridSize가 필요
							float gridSize = 1.0f; // 예: 1.0f, 2.0f 등
							int   tileX = static_cast<int>(floorf(hitPos.x / gridSize));
							int   tileY = static_cast<int>(floorf(hitPos.z / gridSize));

							// 경계 검사
							if (!(tileX < 0 || tileX >= terrainComponent->m_width ||
								tileY < 0 || tileY >= terrainComponent->m_height))
							{
								terrainBrush->m_center = { static_cast<float>(tileX), static_cast<float>(tileY) };

								if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
									terrainComponent->ApplyBrush(*terrainBrush);
								}
							}
						}
					}
				}
			}
		}

	}

	//=========================

	if (useWindow)
	{
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

Mathf::Vector3 SceneViewWindow::ConvertMouseToWorldPosition(Camera* cam, const ImVec2& mouseScreenPos, const ImVec2& imagePos, const ImVec2& imageSize, float depth)
{
	float normX = (mouseScreenPos.x - imagePos.x) / imageSize.x;
	float normY = (mouseScreenPos.y - imagePos.y) / imageSize.y;

	float ndcX = normX * 2.0f - 1.0f;
	float ndcY = (1.0f - normY) * 2.0f - 1.0f;

	XMVECTOR clipPos = XMVectorSet(ndcX, ndcY, depth, 1.0f);

	XMMATRIX proj = cam->CalculateProjection();
	XMMATRIX view = cam->CalculateView();
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, XMMatrixMultiply(view, proj));

	XMVECTOR worldPos = XMVector4Transform(clipPos, invViewProj);
	worldPos = XMVectorScale(worldPos, 1.0f / XMVectorGetW(worldPos));

	XMFLOAT3 result;
	XMStoreFloat3(&result, worldPos);
	return result;
}

Ray SceneViewWindow::CreateRayFromCamera(Camera* cam, const ImVec2& mousePos, const ImVec2& imagePos, const ImVec2& imageSize)
{
	float normX = (mousePos.x - imagePos.x) / imageSize.x;
	float normY = (mousePos.y - imagePos.y) / imageSize.y;

	float ndcX = normX * 2.0f - 1.0f;
	float ndcY = (1.0f - normY) * 2.0f - 1.0f;

	XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
	XMVECTOR farPoint = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

	XMMATRIX view = cam->CalculateView();
	XMMATRIX proj = cam->CalculateProjection();
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);

	nearPoint = XMVector4Transform(nearPoint, invViewProj);
	farPoint = XMVector4Transform(farPoint, invViewProj);

	nearPoint = XMVectorScale(nearPoint, 1.0f / XMVectorGetW(nearPoint));
	farPoint = XMVectorScale(farPoint, 1.0f / XMVectorGetW(farPoint));

	XMVECTOR dir = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));

	Ray ray;
	XMStoreFloat3(&ray.origin, nearPoint);
	XMStoreFloat3(&ray.direction, dir);
	return ray;
}

GameObject* SceneViewWindow::PickObjectFromRay(const Ray& ray, const std::vector<std::shared_ptr<GameObject>>& sceneObjects)
{
	GameObject* selected = nullptr;
	float closestDistance = FLT_MAX;

	for (auto& obj : sceneObjects)
	{
		auto* meshComp = obj->GetComponent<MeshRenderer>();
		if (!meshComp || !meshComp->m_Mesh)
			continue;

		const Mesh* mesh = meshComp->m_Mesh;

		BoundingBox worldAABB;
		worldAABB.Extents = mesh->GetBoundingBox().Extents;
		mesh->GetBoundingBox().Transform(
			worldAABB,
			obj->m_transform.GetWorldMatrix()
		);

		float hitDistance;
		if (worldAABB.Intersects(
			XMLoadFloat3(&ray.origin),
			XMLoadFloat3(&ray.direction),
			hitDistance))
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

std::vector<RayHitResult> SceneViewWindow::PickObjectsFromRay(const Ray& ray, const std::vector<std::shared_ptr<GameObject>>& sceneObjects)
{
	std::vector<RayHitResult> hits;

	for (auto& obj : sceneObjects)
	{
		auto* meshComp = obj->GetComponent<MeshRenderer>();
		auto* cameraComp = obj->GetComponent<CameraComponent>();
		auto* lightComp = obj->GetComponent<LightComponent>();
		if (meshComp && meshComp->m_Mesh)
		{
			const Mesh* mesh = meshComp->m_Mesh;

			BoundingBox worldAABB;
			mesh->GetBoundingBox().Transform(
				worldAABB,
				obj->m_transform.GetWorldMatrix()
			);

			float hitDistance;
			if (worldAABB.Intersects(XMLoadFloat3(&ray.origin), XMLoadFloat3(&ray.direction), hitDistance))
			{
				hits.push_back({ obj.get(), hitDistance });
			}
		}
		else if (cameraComp)
		{
			BoundingBox worldAABB;
			worldAABB = cameraComp->GetEditorBoundingBox();

			float hitDistance;
			if (worldAABB.Intersects(XMLoadFloat3(&ray.origin), XMLoadFloat3(&ray.direction), hitDistance))
			{
				hits.push_back({ obj.get(), hitDistance });
			}
		}
		else if (lightComp)
		{
			BoundingBox worldAABB;
			worldAABB = lightComp->GetEditorBoundingBox();

			float hitDistance;
			if (worldAABB.Intersects(XMLoadFloat3(&ray.origin), XMLoadFloat3(&ray.direction), hitDistance))
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
#endif // !DYNAMICCPP_EXPORTS