#pragma once
#include "IRenderPass.h"
#include "DeviceResources.h"
#include "Texture.h"
#include "Sampler.h"
#include "Shader.h"
#include "Core.Coroutine.h"
// Guillotine Algorithm
// 배치할 위치를 찾고 공간을 나눔.

class RenderScene;
class PositionMapPass;
class LightMapPass;
namespace lm {
	struct Rect {
		int x = 0, y = 0, w = 0, h = 0;
		void* data = nullptr; // 메쉬 렌더러의 포인터
		Mathf::Matrix worldMat;
	};
	struct alignas(16) Triangle{
		XMVECTOR v0, v1, v2;
		XMVECTOR n0, n1, n2;
		XMFLOAT2 uv0, uv1, uv2;
		XMFLOAT2 lightmapUV0, lightmapUV1, lightmapUV2;
		int lightmapIndex = -1;
		int3 pad = { 0,0,0 };
	};

	struct AABB {
		Mathf::Vector3 min{ FLT_MAX, FLT_MAX, FLT_MAX };
		int pad = 0;
		Mathf::Vector3 max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		int pad2 = 0;
		void expand(const Mathf::Vector3& p) {
			if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z)) {
				Debug->LogError("Invalid vector passed to AABB::expand");
				return;
			}
			min = Mathf::Vector3::Min(min, p);
			max = Mathf::Vector3::Max(max, p);
		}

		void expand(const AABB& other) {
			expand(other.min);
			expand(other.max);
		}
	};

	struct alignas(16) BVHNode
	{
		AABB bounds;
		int left = -1;
		int right = -1;
		int start = -1;  // triangle index range
		int end = -1;
		bool32 isLeaf = false;
		int3 pad = { 0,0,0 };
	};


	


	class LightMap final : public IRenderPass
	{
	public:
		LightMap();
		~LightMap();

		void GenerateLightMap(
			RenderScene* scene,
			const std::unique_ptr<PositionMapPass>& m_pPositionMapPass,
			const std::unique_ptr<LightMapPass>& m_pLightMapPass
		);
	private:
		void SetScene(RenderScene* scene) { m_renderscene = scene; }

		void Prepare();
		void PrepareRectangles();
		void CalculateRectangles();

		void PrepareTriangles();

		void DrawRectangles(const std::unique_ptr<PositionMapPass>& m_pPositionMapPass);

		void DilateLightMap(); // 외각선방향으로 확장이지만 아직 수정해야함.
		void DirectBlur();

		void DrawIndirectLight(const std::unique_ptr<PositionMapPass>& m_pPositionMapPass);

	private:
		void CreateLightMap();
		void ClearLightMaps();

	public:
		std::vector<Texture*> lightmaps;
		std::vector<Texture*> indirectMaps;
		std::vector<Texture*> environmentMaps;
		Texture* tempTexture = nullptr;
		Texture* envMap = nullptr;

		ID3D11Texture2D* imgTexture = nullptr;
		ID3D11ShaderResourceView* imgSRV = nullptr;

		// IRenderPass을(를) 통해 상속됨
		void Initialize();
		void Execute(RenderScene& scene, Camera& camera) override;
		virtual void Resize() override;

	public:
		int canvasSize = 1024;
		int padding = 4;
		float bias = 0.0057f;
		int rectSize = 40;
		int leafCount = 4;

		int sampleCount = 4;
		int indirectCount = 2;

		// 외각선에 픽셀 추가.
		int dilateCount = 1;
		// 라이트맵 블러처리.
		int directMSAACount = 1;
		// 간접광 블러처리.
		int indirectMSAACount = 1;

		std::vector<BVHNode> bvhNodes;
	private:
		std::vector<Rect> rects;

		ComPtr<ID3D11Buffer> m_Buffer{};
		ComPtr<ID3D11Buffer> m_transformBuf{};
		ComPtr<ID3D11Buffer> m_lightBuf{};
		ComPtr<ID3D11Buffer> m_settingBuf{};
		ComPtr<ID3D11Buffer> m_indirect1{};

		ComputeShader* m_computeShader{};
		ComputeShader* m_edgeComputeShader{};
		ComputeShader* m_edgeCoverComputeShader{};
		ComputeShader* m_MSAAcomputeShader{};
		ComputeShader* m_indirectLightShader{};
		ComputeShader* m_AddTextureColor{};

		RenderScene* m_renderscene = nullptr;
		Sampler* sample = nullptr;
		Sampler* pointSample = nullptr;

		std::vector<Triangle> m_trianglesInScene;
		std::vector<int> m_triIndices;
		//std::unordered_map<std::string, std::vector<Triangle>> m_meshesTriangles;
		ID3D11Buffer* triangleBuffer = nullptr;
		ID3D11Buffer* indiceBuffer = nullptr;
		ID3D11Buffer* bvhBuffer = nullptr;
		ID3D11ShaderResourceView* TriangleBufferSRV = nullptr;
		ID3D11ShaderResourceView* TriangleIndiceBufferSRV = nullptr;
		ID3D11ShaderResourceView* BVHBufferSRV = nullptr;

		ID3D11Buffer* structuredLightBuffer = nullptr;
		ID3D11ShaderResourceView* structuredLightBufferSRV = nullptr;
		ID3D11ShaderResourceView* textureArraySRV = nullptr;

		Coroutine<> GenerateLightmapCoroutine(
			RenderScene* scene,
			const std::unique_ptr<PositionMapPass>& m_pPositionMapPass,
			const std::unique_ptr<LightMapPass>& m_pLightMapPass
		);

		inline int BuildBVH(std::vector<Triangle>& tris, std::vector<int>& triIndices, int start, int end, int depth = 0)
		{
			BVHNode node;

			// 현재 노드의 AABB 계산
			AABB bounds;
			for (int i = start; i < end; ++i)
			{
				const Triangle& t = tris[triIndices[i]];
				AABB triBox;
				triBox.expand(t.v0);
				triBox.expand(t.v1);
				triBox.expand(t.v2);
				bounds.expand(triBox);
			}
			node.bounds = bounds;

			int triCount = end - start;
			if (triCount <= leafCount) // leaf 조건
			{
				node.start = start;
				node.end = end;
				node.isLeaf = true;
				int index = (int)bvhNodes.size();
				bvhNodes.push_back(node);
				return index;
			}

			// 축 선택 (X, Y, Z 순환)
			int axis = depth % 3;
			std::sort(triIndices.begin() + start, triIndices.begin() + end, [&](int a, int b) {
				Mathf::Vector3 ca = (tris[triIndices[a]].v0 + tris[triIndices[a]].v1 + tris[triIndices[a]].v2) / 3.0f;
				Mathf::Vector3 cb = (tris[triIndices[b]].v0 + tris[triIndices[b]].v1 + tris[triIndices[b]].v2) / 3.0f;
				if (axis == 0) return ca.x < cb.x;
				else if (axis == 1) return ca.y < cb.y;
				else return ca.z < cb.z;
				});

			int mid = (start + end) / 2;

			int nodeIndex = (int)bvhNodes.size();
			bvhNodes.push_back(BVHNode{}); // placeholder

			int left = BuildBVH(tris, triIndices, start, mid, depth + 1);
			int right = BuildBVH(tris, triIndices, mid, end, depth + 1);

			node.left = left;
			node.right = right;
			bvhNodes[nodeIndex] = node;

			return nodeIndex;
		}
	};


}
