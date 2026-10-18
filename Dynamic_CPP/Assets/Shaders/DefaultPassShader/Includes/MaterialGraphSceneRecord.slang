#ifndef LX_MATERIAL_SCENE_RECORD
#define LX_MATERIAL_SCENE_RECORD
struct LXSceneGeometryIdentity
{
    uint2 geometryKey; uint2 objectId; uint2 incarnation; uint2 instanceId; uint2 materialSlot;
    uint lodIndex; uint primitiveCount; uint version; uint3 reserved;
};
// Shared ABI with SceneGpuRecord: 64 + 48 + 48 + 4*16 = 224 bytes.
struct LXSceneGpuRecord
{
    LXSceneGeometryIdentity identity;
    float4 worldColumns[3];
    float4 previousWorldColumns[3];
    uint4 geometry;
    uint4 material;
    uint4 surface;
    uint4 history;
};
#endif
