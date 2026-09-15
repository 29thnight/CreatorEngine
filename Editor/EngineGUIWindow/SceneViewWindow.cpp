#include "EditorModelPlacement.h"
#include "EditorTheme.h"
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
#include "EditorIcons.h"
#include "Scene.h"
#include "Camera.h"
#include "GameObjectCommand.h"
#include "CameraComponent.h"
#include "FoliageComponent.h"
#include "RectTransformComponent.h"
#include "LightComponent.h"
#include "Entity.h"
#include <cstdio>

#include <unordered_map>
#include "DataSystem.h"
#include "PrefabUtility.h"
#include "InputManager.h"
#include "Terrain.h"
#include "EditorSessionState.h"
#include "EditorAssetPresentation.h"
#include "RuntimeSettings.h"
#include "Mathematics.Intersect.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "Windows/EditorViewportWindows.h"

#include <cmath>
#include <cstring>
#include <mathematics/transform.hpp>

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

namespace
{
	// 창 상태의 유일한 자리. 표가 아니라 이 TU 가 든다 — 표에 타입 소거를
	// 들이지 않으려는 선택이고, 같은 창을 두 벌 띄울 요구가 생기면 그때
	// 표로 올린다. 구조체가 이미 하나로 모여 있어 그 이사는 기계적이다.
	SceneViewWindow& scene_view_state()
	{
		static SceneViewWindow state;
		return state;
	}
}

void editor::windows::draw_scene_view()
{
	scene_view_state().RenderSceneViewWindow();
}

void SceneViewWindow::RenderSceneViewWindow()
{
	// 빌린 것들은 매 프레임 정본에서 다시 유도한다(헤더 주석 참고).
	m_editorCameraRig = EditorSessionState::Get().CameraRig();
	m_editorCamera = m_editorCameraRig ? &m_editorCameraRig->GetCamera() : nullptr;
	m_gizmoRenderer = GizmoRenderer::GetActive();

	auto scene = SceneManagers->GetActiveScene();
	if (!scene || !m_editorCamera) return;
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
    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = ImGui::GetContentRegionAvail();
    if (imageSize.x <= 0.f || imageSize.y <= 0.f) return;
    const ImVec2 imageMax{imageMin.x + imageSize.x, imageMin.y + imageSize.y};
    const auto displayed = EnhancedSceneRenderer::GetLiveDisplayTexture(EnhancedLiveDisplayTarget::Editor);
    m_canvas = editor::LayoutViewportCanvas(editor::viewport_fit::fill, imageMin, imageSize,
        {static_cast<float>(displayed.width), static_cast<float>(displayed.height)}, ImGui::GetIO().DisplayFramebufferScale);
    // Reserve the canvas without taking ImGui's active/hovered item: transform
    // gizmos must be able to acquire the mouse over the rendered image.
    ImGui::Dummy(imageSize);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(imageMin, imageMax, IM_COL32(20, 20, 23, 255));
    // W4: 표시 신호를 Game 쪽과 같은 2단으로 읽는다. `active` 는 "그릴 카메라가
    // 있는가", 텍스처 ID 는 "그림이 준비됐는가" 이고 둘은 다른 프레임에 참이 된다.
    // 예전에는 씬 쪽이 둘째만 보아서, 카메라가 없는 것과 첫 프레임을 기다리는 것이
    // 똑같이 빈 검정으로 보였다 — 모드가 하나로 합쳐진 뒤에는 그 구분이 더 필요하다.
    // 스냅샷을 값으로 받는다 — `Get` 이 돌려주는 참조를 임시 객체에서 바로 묶으면
    // 그 임시가 문장 끝에 죽어 매달린 참조가 된다.
    const EnhancedLiveDisplaySnapshot displaySnapshot =
        EnhancedSceneRenderer::GetLiveDisplaySnapshot();
    if (!displaySnapshot.Get(EnhancedLiveDisplayTarget::Editor).active)
    {
        const char* noCamera = "No editor camera";
        const ImVec2 textSize = ImGui::CalcTextSize(noCamera);
        draw->AddText({ imageMin.x + (imageSize.x - textSize.x) * .5f,
                        imageMin.y + (imageSize.y - textSize.y) * .5f },
            ImGui::GetColorU32(ImVec4(1.f, 0.f, 0.f, 1.f)), noCamera);
    }
    else if (displayed.textureId && m_canvas.valid)
    {
        draw->AddImage(displayed.textureId, m_canvas.clipMin, m_canvas.clipMax,
            m_canvas.uvMin, m_canvas.uvMax);
    }
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist();
    m_overlay.Draw(imageMin, imageMax, *m_editorCameraRig, m_gizmoRenderer, m_canvas);
    if (!m_canvas.valid) return;
    // 기즈모는 image 사각형을 받는다 — 잘린 부분까지 포함한 소스 전체의 자리라야
    // 화면 밖으로 밀려난 조작점의 투영이 맞는다. 제목표시줄 보정은 없다(원점이 content).
    //
    // ★ 그런데 ImGuizmo 는 `Manipulate` 첫 줄에서 그 사각형을 **창 클립과 교차하지
    //   않고** 클립으로 민다(`PushClipRect(rect, false)`, ImGuizmo.cpp). crop 의 image
    //   사각형은 캔버스 밖까지 뻗으므로 그대로 두면 기즈모가 계층·인스펙터·탭 줄·
    //   브라우저 위로 그려진다 — W4 가 SetRect 를 창 사각형에서 image 사각형으로
    //   바꾸며 낸 결함이고, 카메라를 돌려 객체가 캔버스를 벗어날 때 드러났다.
    //   ImGuizmo 안을 못 만지므로, 이 함수가 끝날 때 그 뒤에 쌓인 draw 명령의 클립을
    //   캔버스 가시 사각형으로 되잡는다. 조기 반환이 여럿이라 RAII 로 건다.
    struct GizmoClipScope
    {
        ImDrawList* draw; int firstCommand; ImVec2 clipMin, clipMax;
        ~GizmoClipScope()
        {
            for (int i = firstCommand; i < draw->CmdBuffer.Size; ++i)
            {
                ImVec4& clip = draw->CmdBuffer[i].ClipRect;
                clip.x = ImMax(clip.x, clipMin.x); clip.y = ImMax(clip.y, clipMin.y);
                clip.z = ImMin(clip.z, clipMax.x); clip.w = ImMin(clip.w, clipMax.y);
                if (clip.z < clip.x) clip.z = clip.x;
                if (clip.w < clip.y) clip.w = clip.y;
            }
        }
    } gizmoClip{ draw, draw->CmdBuffer.Size, m_canvas.clipMin, m_canvas.clipMax };
    const ImVec2 imageExtent = m_canvas.ImageExtent();
    ImGuizmo::SetRect(m_canvas.imageMin.x, m_canvas.imageMin.y, imageExtent.x, imageExtent.y);
    ImGuizmo::SetOrthographic(cam->m_isOrthographic);
    const auto view = cam->CalculateView();
    const auto projection = cam->CalculateProjectionForAspect(m_canvas.sourceAspect);
    std::memcpy(cameraView, &view.m[0][0], sizeof(view));
    std::memcpy(cameraProjection, &projection.m[0][0], sizeof(projection));
    const bool selectMode = m_overlay.operation == 0;
    const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE, ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    const auto mCurrentGizmoOperation = operations[m_overlay.operation];
    const auto mCurrentGizmoMode = m_overlay.local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    float* activeSnap = m_overlay.ActiveSnap();
    float snap[3]{activeSnap ? *activeSnap : 1.f, activeSnap ? *activeSnap : 1.f, activeSnap ? *activeSnap : 1.f};
    const bool useSnap = activeSnap != nullptr;
    const bool pointerInCanvas = ImGui::IsMouseHoveringRect(m_canvas.clipMin, m_canvas.clipMax) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool canvasInput = pointerInCanvas && !m_overlay.blocksPointer;

    auto* editScene = SceneManagers->GetActiveScene();
    const bool selectionEditable = !EditorObjectOperations::IsEditLocked(obj, true) &&
        std::all_of(editScene->m_selectedEntities.begin(), editScene->m_selectedEntities.end(),
            [](Entity* entity) { return entity && !EditorObjectOperations::IsEditLocked(entity, true); });
    if (obj && selectionEditable && !selectMode)
    {
        // ImGuizmo 는 Manipulate 한 번으로 그림과 입력을 함께 한다. 그래서 오버레이 위에
        // 포인터가 있다는 이유로 이 블록을 건너뛰면 입력만이 아니라 기즈모 자체가 사라진다 —
        // 툴바에 마우스를 올리면 기즈모가 꺼지고 빼면 켜지던 증상이 이것이었다. 그림은 언제나
        // 그리고 입력만 막는다. 다만 Enable(false) 는 ComputeColors 를 inactiveColor 로 덮어
        // 기즈모를 회색으로 만들므로 hover 내내 끄지 않고, 실제로 붙잡을 수 있는 프레임 —
        // 즉 새 클릭이 들어오는 프레임 — 에만 끈다(ImGuizmo::CanActivate 는 IsMouseClicked(0)
        // 없이는 잡지 않으므로 그 한 프레임만 막으면 충분하다). Enable 은 전역 상태라 되돌린다.
        struct GizmoInputScope
        {
            explicit GizmoInputScope(bool enable) noexcept { ImGuizmo::Enable(enable); }
            ~GizmoInputScope() { ImGuizmo::Enable(true); }
        } gizmoInputScope{ canvasInput || ImGuizmo::IsUsing() ||
            !ImGui::IsMouseClicked(ImGuiMouseButton_Left) };
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
				nullptr, nullptr);

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
				nullptr, nullptr);

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

	if (canvasInput && ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		m_editorCameraRig->HandleMovement(Time->GetElapsedSeconds());
	}

	if (selectionEditable && ImGui::IsWindowFocused() && !m_overlay.blocksShortcuts && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
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
	else if (ImGui::IsWindowFocused() && !m_overlay.blocksShortcuts && ImGui::IsKeyDown(ImGuiKey_F)) {
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
    if (editorTerrainBrush && EditorObjectOperations::IsEditLocked(sceneSelectedObj, true))
        editorTerrainBrush->m_isEditMode = false;
	if(nullptr == editorTerrainBrush || false == editorTerrainBrush->m_isEditMode)
	{
		if (!useGizmo &&
			canvasInput &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			float closest = FLT_MAX;
			ImVec2 mousePos = ImGui::GetMousePos();
			Ray ray = CreateRayFromCamera(cam, mousePos);

			const auto& sceneObjects = SceneManagers->GetActiveScene()->m_Entities;
			auto hits = editor::picking::GatherRayHits(ray, sceneObjects);

			if (!hits.empty())
			{
				// 이번 클릭이 맞힌 더미의 신원. 순환을 이것으로 가르는 이유와
				// 포인터가 아니라 핸들인 이유는 AdvanceCycleIndex 주석 참고.
				std::vector<EntityHandle> cycleIdentity;
				cycleIdentity.reserve(hits.size());
				for (const RayHitResult& hit : hits)
					cycleIdentity.push_back(scene->HandleOf(hit.object->m_index));

				m_hitResults = hits;
				const std::size_t chosen = editor::picking::AdvanceCycleIndex(
					cycleIdentity, m_hitCycleIdentity, m_currentHitIndex);
				Entity* selected = m_hitResults[chosen].object;

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
				m_hitCycleIdentity.clear();
				m_currentHitIndex = 0;
			}
		}

		ImRect dropRect = ImRect(imageMin, imageMax);
        if (canvasInput && ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("MyDropTarget")))
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Model", ImGuiDragDropFlags_AcceptBeforeDelivery))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const Ray ray = CreateRayFromCamera(cam, mouse);
                float distance = 0;
                std::optional<math::vector3> position;
                if (RayIntersectsPlane(ray, { 0, 1, 0 }, { 0, 0, 0 }, distance))
                    position = ray.origin + ray.direction * distance;
                if (payload->IsDelivery() && scene)
                {
                    const file::path filename = static_cast<const char*>(payload->Data);
                    const file::path path = PathFinder::Relative("Models\\") / filename.filename();
                    Editor::ModelPlacement::Get().Execute(scene->GetSceneId(), path.string(), position);
                }
                else
                {
                    ImGui::GetWindowDrawList()->AddCircle(mouse, 8.0f, IM_COL32(230, 200, 80, 255), 16, 2.0f);
                    ImGui::SetTooltip("Drop to load and place model");
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
					Debug::PrintLog(spdlog::level::err, "SkyBox 변경 실패: " + skyError);
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
				if (canvasInput)
				{
					ImVec2 mousePos = ImGui::GetMousePos();
					Ray ray = CreateRayFromCamera(cam, mousePos);
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

}

math::vector3 SceneViewWindow::ConvertMouseToWorldPosition(Camera* cam, const ImVec2& mouseScreenPos, float depth)
{
	const auto uv = m_canvas.SourceUV(mouseScreenPos);
	const float normX = uv.x;
	const float normY = uv.y;

	const float ndcX = normX * 2.0f - 1.0f;
	const float ndcY = (1.0f - normY) * 2.0f - 1.0f;
	const math::vector4 clipPosition{ ndcX, ndcY, depth, 1.0f };
	const math::matrix4x4 inverseViewProjection =
		math::inverse(cam->CalculateView() * cam->CalculateProjectionForAspect(m_canvas.sourceAspect));
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

Ray SceneViewWindow::CreateRayFromCamera(Camera* cam, const ImVec2& mousePos)
{
	const math::vector3 nearPoint = ConvertMouseToWorldPosition(
		cam, mousePos, 0.0f);
	const math::vector3 farPoint = ConvertMouseToWorldPosition(
		cam, mousePos, 1.0f);
	return Ray{ nearPoint, math::normalize(farPoint - nearPoint) };
}

namespace editor::picking
{

std::vector<Entity*> CollectOccupants(
	const std::vector<std::unique_ptr<Entity>>& slots)
{
	std::vector<Entity*> occupants;
	occupants.reserve(slots.size());
	for (const std::unique_ptr<Entity>& slot : slots)
	{
		// 구멍은 여기서만 걷어낸다. 이 한 줄이 없어서 2026-09-14 에 씬뷰
		// 클릭 한 번이 프로세스를 죽였다(덤프: 읽기 주소 0x150 — null this
		// + Entity::m_componentTypeMask 오프셋). 저장소의 다른 슬롯맵 순회
		// 열넷은 전부 같은 검사를 갖고 있었고 피킹만 없었다.
		if (nullptr == slot) continue;
		occupants.push_back(slot.get());
	}
	return occupants;
}

std::vector<RayHitResult> GatherRayHits(
	const Ray& ray, const std::vector<std::unique_ptr<Entity>>& slots)
{
	std::vector<RayHitResult> hits;
	const math::ray pickRay{ ray.origin, ray.direction };

	for (Entity* obj : CollectOccupants(slots))
	{
		auto* meshComp = obj->GetComponent<MeshRenderer>();
		auto* cameraComp = obj->GetComponent<CameraComponent>();
		auto* lightComp = obj->GetComponent<LightComponent>();
		// I5-D5b — "그릴 메시가 있는가"는 창구가 판정한다. legacy m_Mesh를
		// 직접 가드로 쓰면 D4f의 은퇴가 이 조건을 통째로 거짓으로 만들어
		// 피킹이 조용히 죽는다(선택 불가는 렌더 회귀로 안 잡힌다).
		if (meshComp && meshComp->HasRenderableMesh())
		{
			const math::aabb worldAABB = meshComp->GetBoundingBox();
			if (worldAABB.is_empty()) continue;

			float hitDistance;
			if (math::raycast(pickRay, worldAABB, hitDistance))
			{
				hits.push_back({ obj, hitDistance });
			}
		}
		else if (cameraComp)
		{
			const math::aabb worldAABB = cameraComp->GetEditorBoundingBox();

			float hitDistance;
			if (math::raycast(pickRay, worldAABB, hitDistance))
			{
				hits.push_back({ obj, hitDistance });
			}
		}
		else if (lightComp)
		{
			const math::aabb worldAABB = lightComp->GetEditorBoundingBox();

			float hitDistance;
			if (math::raycast(pickRay, worldAABB, hitDistance))
			{
				hits.push_back({ obj, hitDistance });
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

std::size_t AdvanceCycleIndex(
	const std::vector<EntityHandle>& identity,
	std::vector<EntityHandle>& lastIdentity,
	std::size_t& cursor)
{
	if (identity.empty())
	{
		lastIdentity.clear();
		cursor = 0;
		return 0;
	}

	// 순환은 **같은 더미를 다시 찍었을 때만** 다음 것으로 넘어간다. 예전에는
	// 리셋 조건이 "아무것도 못 맞혔을 때" 하나뿐이라, 지점을 옮겨 찍어도
	// 커서가 계속 올라갔다 — 바닥 위의 모델을 두 번째로 클릭하면 index 1 →
	// 바닥이 잡혔고, 인스펙터는 그 선택을 그대로 그리므로 사용자는 모델을
	// 편집한다고 믿으면서 바닥의 Scale 을 고쳤다(2026-09-14 사고).
	//
	// 신원을 Entity* 가 아니라 EntityHandle 로 재는 이유는 세대다. 파괴된
	// 슬롯 자리에 새 엔티티가 같은 힙 주소로 들어오면 포인터 비교는 ABA 로
	// "같다" 고 답하고, 그러면 이 리셋이 다시 조용히 안 걸린다.
	if (identity != lastIdentity)
	{
		lastIdentity = identity;
		cursor = 0;
	}

	const std::size_t chosen = cursor % identity.size();
	cursor = chosen + 1;
	return chosen;
}

}
