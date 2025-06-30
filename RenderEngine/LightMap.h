#pragma once
#ifndef DYNAMICCPP_EXPORTS
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
		float3 tempColor = { 0,0,0 };
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
		bool CalculateRectangles();

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
		std::vector<Texture*> directionalMaps;
		Texture* tempTexture = nullptr;
		Texture* envMap = nullptr;

		ID3D11Texture2D* imgTexture = nullptr;
		ID3D11ShaderResourceView* imgSRV = nullptr;

		// IRenderPass을(를) 통해 상속됨
		void Initialize();
		void Execute(RenderScene& scene, Camera& camera) override;
		virtual void Resize(uint32_t width, uint32_t height) override;

	public:
		int canvasSize = 1024;
		int padding = 4;
		float bias = 0.0057f;
		int rectSize = 400;
		int leafCount = 4;

		int indirectSampleCount = 512;
		int indirectCount = 2;

		// 외각선에 픽셀 추가.
		//int dilateCount = 0; // 이제 사용 안해도될듯 포지면맵 단계에서 미리 픽셀 늘려두면 됨.
		// 라이트맵 블러처리.
		int directMSAACount = 2;
		// 간접광 블러처리.
		int indirectMSAACount = 2;

		bool useEnvironmentMap = true;

		std::vector<BVHNode> bvhNodes;
	private:
		std::vector<Rect> rects;

		ComPtr<ID3D11Buffer> m_Buffer{};
		ComPtr<ID3D11Buffer> m_transformBuf{};
		ComPtr<ID3D11Buffer> m_lightBuf{};
		ComPtr<ID3D11Buffer> m_settingBuf{};
		ComPtr<ID3D11Buffer> m_indirect1{};
		ComPtr<ID3D11Buffer> m_CBHammersley{};

		ComputeShader* m_computeShader{};
		ComputeShader* m_edgeComputeShader{};
		ComputeShader* m_edgeCoverComputeShader{};
		ComputeShader* m_MSAAcomputeShader{};
		ComputeShader* m_indirectLightShader{};
		ComputeShader* m_AddTextureColor{};
		ComputeShader* m_NormalizeTextureColor{};

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

	private:
		void UnBindLightmapPS();

		float RadicalInverse_VdC(UINT bits)
		{
			bits = (bits << 16) | (bits >> 16);
			bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
			bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
			bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
			bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
			return float(bits) * 2.3283064365386963e-10;
		}

		float2 HammersleySample(UINT i, UINT N)
		{
			return float2(float(i) / float(N), RadicalInverse_VdC(i));
		}


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
			int debugstart = start;
			int debugend = end;
			std::sort(triIndices.begin() + start, triIndices.begin() + end, [&](int a, int b) {
				//assert(a >= 0 && a <= end && b >= 0 && b <= end);
				int debugstart = start;
				int debugend = end;
				Triangle& t1 = tris[a];//tris[triIndices[a]];
				Triangle& t2 = tris[b];//tris[triIndices[b]];
				Mathf::Vector3 ca = (t1.v0 + t1.v1 + t1.v2) / 3.0f;
				Mathf::Vector3 cb = (t2.v0 + t2.v1 + t2.v2) / 3.0f;
				if (axis == 0) { 
					t2.tempColor = { 1,0,0 };
					return ca.x < cb.x; }
				else if (axis == 1) { 
					t2.tempColor = { 0,1,0 };
					return ca.y < cb.y; 
				}
				else { 
					t2.tempColor = { 0,0,1 };
					return ca.z < cb.z; 
				}
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
#endif // !DYNAMICCPP_EXPORTS