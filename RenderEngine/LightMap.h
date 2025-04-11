#pragma once
#include "IRenderPass.h"
#include "DeviceResources.h"
#include "Texture.h"
#include "Sampler.h"
#include "Shader.h"
// Guillotine Algorithm
// 배치할 위치를 찾고 공간을 나눔.

class RenderScene;
class PositionMapPass;
class NormalMapPass;
class LightmapShadowPass;
namespace lm {
	struct Rect {
		int x = 0, y = 0, w = 0, h = 0;
		void* data = nullptr; // 메쉬 렌더러의 포인터
		Mathf::Matrix worldMat;
	};

	class LightMap final : public IRenderPass
	{
	public:
		LightMap();
		~LightMap();

		void GenerateLightMap(
			RenderScene* scene,
			const std::unique_ptr<LightmapShadowPass>& lightmapShadowPass,
			const std::unique_ptr<PositionMapPass>& m_pPositionMapPass
			//const std::unique_ptr<NormalMapPass>& m_pNormalMapPass
		);
	private:
		void TestPrepare();
		void SetScene(RenderScene* scene) { m_renderscene = scene; }

		void Prepare();
		void PrepareRectangles();
		void CalculateRectangles();
		void DrawRectangles(
			const std::unique_ptr<LightmapShadowPass>& m_pLightmapShadowPass,
			const std::unique_ptr<PositionMapPass>& m_pPositionMapPass
			//const std::unique_ptr<NormalMapPass>& m_pNormalMapPass
		);

	private:
		void CreateLightMap();
		void ClearLightMaps();

	public:
		std::vector<Texture*> lightmaps;

		ID3D11Texture2D* imgTexture = nullptr;
		Texture* edgeTexture = nullptr;
		Texture* envMap = nullptr;
		ID3D11ShaderResourceView* imgSRV = nullptr;
		ID3D11ShaderResourceView* textureArraySRV = nullptr;
		ID3D11ShaderResourceView* structuredBufferSRV = nullptr;

		// IRenderPass을(를) 통해 상속됨
		void Initialize();
		void Execute(RenderScene& scene, Camera& camera) override;
		virtual void Resize() override;

	public:
		int canvasSize = 4096;
		int padding = 4;
		float bias = 0.0057f;
		int rectSize = 40;
	private:
		std::vector<Rect> rects;

		ComPtr<ID3D11Buffer> m_Buffer{};
		ComPtr<ID3D11Buffer> m_transformBuf{};
		ComPtr<ID3D11Buffer> m_lightBuf{};
		ComPtr<ID3D11Buffer> m_settingBuf{};
		ComputeShader* m_computeShader{};
		ComputeShader* m_edgeComputeShader{};
		ComputeShader* m_edgeCoverComputeShader{};

		ComputeShader* m_MSAAcomputeShader{};

		RenderScene* m_renderscene = nullptr;
		Sampler* sample = nullptr;
		Sampler* pointSample = nullptr;
	};
}
