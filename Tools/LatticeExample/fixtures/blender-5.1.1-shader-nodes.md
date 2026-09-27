# Blender 5.1.1 Shader Node 및 편집 노드 목록

실측 설치본: Blender 5.1.1, build `b70da489d7f4`.
[소켓·기본값 전체 JSON](blender-5.1.1-shader-nodes.json)에서 입력·출력 순서, 타입, 기본값, 표시 상태를 확인한다.
enum 변형은 각 속성을 기본 상태에서 하나씩 변경해 관찰했다. 조합은 아직 전수 조사하지 않았다.
현재 LX 예제는 제품용 Material compiler가 없어 아래 노드를 지원 완료로 표시하지 않는다.

| Blender node type | 이름 | 기본 입력 | 기본 출력 | 소켓 변경 enum 상태 | LX 제품 지원 |
|---|---|---:|---:|---:|---|
| `NodeFrame` | Frame | 0 | 0 | 0 | 미구현 |
| `NodeGroupInput` | Group Input | 0 | 1 | 0 | 미구현 |
| `NodeGroupOutput` | Group Output | 1 | 0 | 0 | 미구현 |
| `NodeReroute` | Reroute | 1 | 1 | 0 | 미구현 |
| `ShaderNodeAddShader` | Add Shader | 2 | 1 | 0 | 미구현 |
| `ShaderNodeAmbientOcclusion` | Ambient Occlusion | 3 | 2 | 0 | 미구현 |
| `ShaderNodeAttribute` | Attribute | 0 | 4 | 0 | 미구현 |
| `ShaderNodeBackground` | Background | 3 | 1 | 0 | 미구현 |
| `ShaderNodeBevel` | Bevel | 2 | 1 | 0 | 미구현 |
| `ShaderNodeBlackbody` | Blackbody | 1 | 1 | 0 | 미구현 |
| `ShaderNodeBrightContrast` | Brightness/Contrast | 3 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfAnisotropic` | Glossy BSDF | 7 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfDiffuse` | Diffuse BSDF | 4 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfGlass` | Glass BSDF | 7 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfHair` | Hair BSDF | 6 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfHairPrincipled` | Principled Hair BSDF | 18 | 1 | 3 | 미구현 |
| `ShaderNodeBsdfMetallic` | Metallic BSDF | 12 | 1 | 1 | 미구현 |
| `ShaderNodeBsdfPrincipled` | Principled BSDF | 31 | 1 | 2 | 미구현 |
| `ShaderNodeBsdfRayPortal` | Ray Portal BSDF | 4 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfRefraction` | Refraction BSDF | 5 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfSheen` | Sheen BSDF | 4 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfToon` | Toon BSDF | 5 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfTranslucent` | Translucent BSDF | 3 | 1 | 0 | 미구현 |
| `ShaderNodeBsdfTransparent` | Transparent BSDF | 2 | 1 | 0 | 미구현 |
| `ShaderNodeBump` | Bump | 5 | 1 | 0 | 미구현 |
| `ShaderNodeCameraData` | Camera Data | 0 | 3 | 0 | 미구현 |
| `ShaderNodeClamp` | Clamp | 3 | 1 | 0 | 미구현 |
| `ShaderNodeCombineColor` | Combine Color | 3 | 1 | 0 | 미구현 |
| `ShaderNodeCombineXYZ` | Combine XYZ | 3 | 1 | 0 | 미구현 |
| `ShaderNodeDisplacement` | Displacement | 4 | 1 | 0 | 미구현 |
| `ShaderNodeEeveeSpecular` | Specular BSDF | 10 | 1 | 0 | 미구현 |
| `ShaderNodeEmission` | Emission | 3 | 1 | 0 | 미구현 |
| `ShaderNodeFloatCurve` | Float Curve | 2 | 1 | 0 | 미구현 |
| `ShaderNodeFresnel` | Fresnel | 2 | 1 | 0 | 미구현 |
| `ShaderNodeGamma` | Gamma | 2 | 1 | 0 | 미구현 |
| `ShaderNodeGroup` | Group | 0 | 0 | 0 | 미구현 |
| `ShaderNodeHairInfo` | Curves Info | 0 | 6 | 0 | 미구현 |
| `ShaderNodeHoldout` | Holdout | 1 | 1 | 0 | 미구현 |
| `ShaderNodeHueSaturation` | Hue/Saturation/Value | 5 | 1 | 0 | 미구현 |
| `ShaderNodeInvert` | Invert Color | 2 | 1 | 0 | 미구현 |
| `ShaderNodeLayerWeight` | Layer Weight | 2 | 2 | 0 | 미구현 |
| `ShaderNodeLightFalloff` | Light Falloff | 2 | 3 | 0 | 미구현 |
| `ShaderNodeLightPath` | Light Path | 0 | 15 | 0 | 미구현 |
| `ShaderNodeMapRange` | Map Range | 12 | 2 | 2 | 미구현 |
| `ShaderNodeMapping` | Mapping | 4 | 1 | 2 | 미구현 |
| `ShaderNodeMath` | Math | 3 | 1 | 26 | 미구현 |
| `ShaderNodeMix` | Mix | 10 | 4 | 2 | 미구현 |
| `ShaderNodeMixRGB` | Mix (Legacy) | 3 | 1 | 0 | 미구현 |
| `ShaderNodeMixShader` | Mix Shader | 3 | 1 | 0 | 미구현 |
| `ShaderNodeNewGeometry` | Geometry | 0 | 9 | 0 | 미구현 |
| `ShaderNodeNormal` | Normal | 1 | 2 | 0 | 미구현 |
| `ShaderNodeNormalMap` | Normal Map | 2 | 1 | 0 | 미구현 |
| `ShaderNodeObjectInfo` | Object Info | 0 | 6 | 0 | 미구현 |
| `ShaderNodeOutputAOV` | AOV Output | 2 | 0 | 0 | 미구현 |
| `ShaderNodeOutputLight` | Light Output | 1 | 0 | 0 | 미구현 |
| `ShaderNodeOutputLineStyle` | Line Style Output | 4 | 0 | 0 | 미구현 |
| `ShaderNodeOutputMaterial` | Material Output | 4 | 0 | 0 | 미구현 |
| `ShaderNodeOutputWorld` | World Output | 2 | 0 | 0 | 미구현 |
| `ShaderNodeParticleInfo` | Particle Info | 0 | 8 | 0 | 미구현 |
| `ShaderNodePointInfo` | Point Info | 0 | 3 | 0 | 미구현 |
| `ShaderNodeRGB` | Color | 0 | 1 | 0 | 미구현 |
| `ShaderNodeRGBCurve` | RGB Curves | 2 | 1 | 0 | 미구현 |
| `ShaderNodeRGBToBW` | RGB to BW | 1 | 1 | 0 | 미구현 |
| `ShaderNodeRadialTiling` | Radial Tiling | 3 | 4 | 0 | 미구현 |
| `ShaderNodeRaycast` | Raycast | 3 | 5 | 0 | 미구현 |
| `ShaderNodeScript` | Script | 0 | 0 | 0 | 미구현 |
| `ShaderNodeSeparateColor` | Separate Color | 1 | 3 | 0 | 미구현 |
| `ShaderNodeSeparateXYZ` | Separate XYZ | 1 | 3 | 0 | 미구현 |
| `ShaderNodeShaderToRGB` | Shader to RGB | 1 | 2 | 0 | 미구현 |
| `ShaderNodeSqueeze` | Squeeze Value (Legacy) | 3 | 1 | 0 | 미구현 |
| `ShaderNodeSubsurfaceScattering` | Subsurface Scattering | 8 | 1 | 2 | 미구현 |
| `ShaderNodeTangent` | Tangent | 0 | 1 | 0 | 미구현 |
| `ShaderNodeTexBrick` | Brick Texture | 10 | 2 | 0 | 미구현 |
| `ShaderNodeTexChecker` | Checker Texture | 4 | 2 | 0 | 미구현 |
| `ShaderNodeTexCoord` | Texture Coordinate | 0 | 7 | 0 | 미구현 |
| `ShaderNodeTexEnvironment` | Environment Texture | 1 | 1 | 0 | 미구현 |
| `ShaderNodeTexGabor` | Gabor Texture | 6 | 3 | 1 | 미구현 |
| `ShaderNodeTexGradient` | Gradient Texture | 1 | 2 | 0 | 미구현 |
| `ShaderNodeTexIES` | IES Texture | 2 | 1 | 0 | 미구현 |
| `ShaderNodeTexImage` | Image Texture | 1 | 2 | 0 | 미구현 |
| `ShaderNodeTexMagic` | Magic Texture | 3 | 2 | 0 | 미구현 |
| `ShaderNodeTexNoise` | Noise Texture | 9 | 2 | 5 | 미구현 |
| `ShaderNodeTexSky` | Sky Texture | 1 | 1 | 2 | 미구현 |
| `ShaderNodeTexVoronoi` | Voronoi Texture | 9 | 5 | 6 | 미구현 |
| `ShaderNodeTexWave` | Wave Texture | 7 | 2 | 0 | 미구현 |
| `ShaderNodeTexWhiteNoise` | White Noise Texture | 2 | 2 | 2 | 미구현 |
| `ShaderNodeUVAlongStroke` | UV Along Stroke | 0 | 1 | 0 | 미구현 |
| `ShaderNodeUVMap` | UV Map | 0 | 1 | 0 | 미구현 |
| `ShaderNodeValToRGB` | Color Ramp | 1 | 2 | 0 | 미구현 |
| `ShaderNodeValue` | Value | 0 | 1 | 0 | 미구현 |
| `ShaderNodeVectorCurve` | Vector Curves | 2 | 1 | 0 | 미구현 |
| `ShaderNodeVectorDisplacement` | Vector Displacement | 3 | 1 | 0 | 미구현 |
| `ShaderNodeVectorMath` | Vector Math | 4 | 2 | 18 | 미구현 |
| `ShaderNodeVectorRotate` | Vector Rotate | 5 | 1 | 4 | 미구현 |
| `ShaderNodeVectorTransform` | Vector Transform | 1 | 1 | 0 | 미구현 |
| `ShaderNodeVertexColor` | Color Attribute | 0 | 2 | 0 | 미구현 |
| `ShaderNodeVolumeAbsorption` | Volume Absorption | 3 | 1 | 0 | 미구현 |
| `ShaderNodeVolumeCoefficients` | Volume Coefficients | 9 | 1 | 4 | 미구현 |
| `ShaderNodeVolumeInfo` | Volume Info | 0 | 4 | 0 | 미구현 |
| `ShaderNodeVolumePrincipled` | Principled Volume | 13 | 1 | 0 | 미구현 |
| `ShaderNodeVolumeScatter` | Volume Scatter | 8 | 1 | 4 | 미구현 |
| `ShaderNodeWavelength` | Wavelength | 1 | 1 | 0 | 미구현 |
| `ShaderNodeWireframe` | Wireframe | 1 | 1 | 0 | 미구현 |

## 생성할 수 없는 RNA 타입

- `ShaderNodeCustomGroup`: Error: Node type ShaderNodeCustomGroup undefined
- `ShaderNodeTree`: Error: Node type ShaderNodeTree undefined
