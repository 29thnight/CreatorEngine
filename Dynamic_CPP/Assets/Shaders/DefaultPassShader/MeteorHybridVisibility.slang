#include "Includes/MaterialGraphSurfacePoint.slang"
#include "Includes/LxMeshletGpuContract.slang"
#include "Includes/MeteorVisibilityContract.slang"
#include "Includes/MeshletGeometryAccess.slang"
cbuffer RasterConstants : register(b0) { float4x4 projection; uint width, height, recordCount, meshletCount; };
StructuredBuffer<LXFrameGeometryRecord> geometryTable : register(t0);
StructuredBuffer<uint4> metadata : register(t1); // visible offset, active/SW/double-sided flags
StructuredBuffer<uint> visibleIds : register(t2);
StructuredBuffer<uint64_t> completedKeys : register(t3);
// Work ABI: {visibleCount, softwareCount, 0, 0}, recordCount+1 prefix words,
// then up to meshletCount pairs {record, meshlet} selected for software raster.
// The dispatch buffer holds two native 12-byte {x,y,z} compute commands.
StructuredBuffer<uint> completedWork : register(t5);
StructuredBuffer<float4> vertices[4096] : register(t16);
ByteAddressBuffer topology[4096] : register(t4112);
RWStructuredBuffer<uint> routes : register(u0);
RWStructuredBuffer<uint64_t> keys : register(u1);
RWStructuredBuffer<uint> work : register(u2);
RWStructuredBuffer<uint> dispatchArguments : register(u3);
static const uint WorkHeaderWords=4u;
static const uint MaximumRecords=4096u;
uint SoftwareWorkBase()
{
    return WorkHeaderWords + recordCount + 1u;
}
uint GroupIndex(uint3 group) { return group.y * 65535u + group.x; }
void WriteDispatch(uint word, uint count)
{
    dispatchArguments[word] = min(count, 65535u);
    dispatchArguments[word + 1u] = max(1u, (count + 65534u) / 65535u);
    dispatchArguments[word + 2u] = 1u;
}
// Vertex streams are object space; the frame record carries object-to-world.
float4 Clip(uint record, uint vertex) {
    const float3 world=MeteorRecordWorldPosition(geometryTable[record],MeteorGeometryPoint(vertices[record-1u],topology[record-1u],vertex).position.xyz);
    return mul(float4(world,1),projection);
}
float2 Screen(float4 clip) { return (clip.xy/clip.w*float2(.5,-.5)+.5)*float2(width,height); }
[numthreads(64,1,1)]
void Initialize(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint index=GroupIndex(group)*64u+lane;
    if(index<width*height) keys[index]=~uint64_t(0);
    if(index<meshletCount) routes[index]=0u;
}
// Scan the visible counts, not the frame's meshlet capacity. One bounded group
// builds record ranges and a GPU dispatch without copying the compacted IDs.
// The first and final stages read their own independently produced visibility.
groupshared uint visiblePrefix[MaximumRecords];
[numthreads(64,1,1)]
void PrepareClassify(uint lane : SV_GroupIndex)
{
    uint scanSize = 1u;
    while (scanSize < recordCount - 1u)
    {
        scanSize <<= 1u;
    }
    for (uint i = lane; i < scanSize; i += 64u)
    {
        const uint record = i + 1u;
        uint count = 0u;
        if (record < recordCount)
        {
            const uint4 meta = metadata[record];
            if ((meta.y & 1u) != 0u)
            {
                count = min(visibleIds[meta.x], geometryTable[record].meshletCount);
            }
        }
        visiblePrefix[i] = count;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 1u; stride < scanSize; stride <<= 1u)
    {
        for (uint i = (lane + 1u) * stride * 2u - 1u; i < scanSize; i += 64u * stride * 2u)
        {
            visiblePrefix[i] += visiblePrefix[i - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0u)
    {
        const uint count = visiblePrefix[scanSize - 1u];
        work[0] = count;
        work[1] = 0u;
        work[2] = work[3] = 0u;
        work[WorkHeaderWords] = 0u;
        work[WorkHeaderWords + recordCount] = count;
        WriteDispatch(0u, count);
        WriteDispatch(3u, 0u);
        visiblePrefix[scanSize - 1u] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = scanSize >> 1u; stride > 0u; stride >>= 1u)
    {
        for (uint i = (lane + 1u) * stride * 2u - 1u; i < scanSize; i += 64u * stride * 2u)
        {
            const uint left = visiblePrefix[i - stride];
            visiblePrefix[i - stride] = visiblePrefix[i];
            visiblePrefix[i] += left;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    for (uint i = lane; i < recordCount - 1u; i += 64u)
    {
        work[WorkHeaderWords + i + 1u] = visiblePrefix[i];
    }
}

groupshared float2 boundsLow[64],boundsHigh[64];
groupshared uint boundsValid[64];
// One group owns one actual visible meshlet. Descriptor selection and every
// branch around a group barrier are uniform. Routes keep immutable addresses.
[numthreads(64,1,1)]
void Classify(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint index = GroupIndex(group);
    if (index >= work[0])
    {
        return;
    }
    // Lower-bound over record ends also skips empty/inactive records.
    uint first = 1u, last = recordCount;
    while (first < last)
    {
        const uint middle = first + (last - first) / 2u;
        if (work[WorkHeaderWords + middle + 1u] <= index)
        {
            first = middle + 1u;
        }
        else
        {
            last = middle;
        }
    }
    const uint record = first;
    const uint4 meta = metadata[record];
    const uint base = (geometryTable[record].reserved.y - 1u) / 128u;
    const uint visibleIndex = index - work[WorkHeaderWords + record];
    const uint meshlet = visibleIds[meta.x + 1u + visibleIndex];
    if ((meta.y & 2u) == 0u)
    {
        if (lane == 0u)
        {
            routes[base + meshlet] = 1u;
        }
        return;
    }
    const LXMeshletDescriptor d = LXLoadMeshlet(topology[record - 1u], meshlet);
    bool software = d.vertexCount != 0u;
    float2 lo = float2(1e30), hi = float2(-1e30);
    uint valid = 1u;
    for (uint batch = 0u; batch < d.vertexCount && software; batch += 64u)
    {
        const uint vertex = batch + lane;
        if (vertex < d.vertexCount)
        {
            const float4 c = Clip(record, LXMeshletVertex(topology[record - 1u], d, vertex));
            // Clipped/nonfinite work remains in HW; no partial SW coverage.
            const bool inside = all(isfinite(c)) && c.w > 0 && c.z >= 0 && c.z < c.w && all(abs(c.xy) <= c.w);
            valid &= uint(inside);
            if (inside)
            {
                const float2 p = Screen(c);
                lo = min(lo, p);
                hi = max(hi, p);
            }
        }
        boundsLow[lane] = lo;
        boundsHigh[lane] = hi;
        boundsValid[lane] = valid;
        GroupMemoryBarrierWithGroupSync();
        for (uint stride = 32u; stride > 0u; stride >>= 1u)
        {
            if (lane < stride)
            {
                boundsLow[lane] = min(boundsLow[lane], boundsLow[lane + stride]);
                boundsHigh[lane] = max(boundsHigh[lane], boundsHigh[lane + stride]);
                boundsValid[lane] &= boundsValid[lane + stride];
            }
            GroupMemoryBarrierWithGroupSync();
        }
        // Partial bounds only expand. Once either dimension exceeds 8 pixels,
        // later vertices cannot make the meshlet eligible for software raster.
        software = boundsValid[0] != 0u && all(ceil(boundsHigh[0]) - floor(boundsLow[0]) <= 8.0);
        // Every lane consumes the reduction before the next batch overwrites it.
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0u)
    {
        if (software)
        {
            uint slot;
            InterlockedAdd(work[1], 1u, slot);
            work[SoftwareWorkBase() + slot * 2u] = record;
            work[SoftwareWorkBase() + slot * 2u + 1u] = meshlet;
        }
        routes[base + meshlet] = software ? 2u : 1u;
    }
}
[numthreads(1,1,1)]
void PrepareSoftware()
{
    WriteDispatch(3u, completedWork[1]);
}
int Edge(int2 a,int2 b,int2 p) { int2 d=b-a,q=p-a;return d.x*q.y-d.y*q.x; }
bool TopLeft(int2 a,int2 b) {int2 d=b-a;return d.y<0 || (d.y==0 && d.x>0);}
float Area(float2 a,float2 b,float2 p) {float2 d=b-a,q=p-a;return d.x*q.y-d.y*q.x;}
[numthreads(64,1,1)]
void Rasterize(uint3 group : SV_GroupID,uint lane : SV_GroupIndex)
{
    const uint index=GroupIndex(group);
    if (index >= completedWork[1])
    {
        return;
    }
    const uint record=completedWork[SoftwareWorkBase()+index*2u];
    const uint meshlet=completedWork[SoftwareWorkBase()+index*2u+1u];
    LXMeshletDescriptor d=LXLoadMeshlet(topology[record-1u],meshlet);
    for(uint triangle=lane;triangle<d.triangleCount;triangle+=64u) {
        LXGeometryPrimitive primitive=LXLoadGeometryPrimitive(topology[record-1u],d,triangle);
        float4 ca=Clip(record,primitive.vertices.x),cb=Clip(record,primitive.vertices.y),cc=Clip(record,primitive.vertices.z);
        float2 pa=Screen(ca),pb=Screen(cb),pc=Screen(cc);
        int2 a=int2(round(pa*256.0)),b=int2(round(pb*256.0)),c=int2(round(pc*256.0));
        int area=Edge(a,b,c);
        if(area==0 || (area<0 && (metadata[record].y&4u)==0u)) continue;
        if(area<0) {int2 tmp=b;b=c;c=tmp;}
        int2 lo=max(int2(0),int2(ceil((float2(min(a,min(b,c)))-128.0)/256.0)));
        int2 hi=min(int2(width,height)-1,int2(floor((float2(max(a,max(b,c)))-128.0)/256.0)));
        float denominator=Area(pa,pb,pc);
        if(!isfinite(denominator) || denominator==0.0) continue;
        for(int y=lo.y;y<=hi.y;++y) for(int x=lo.x;x<=hi.x;++x) {
            int2 p=int2(x,y)*256+128;
            int e0=Edge(a,b,p),e1=Edge(b,c,p),e2=Edge(c,a,p);
            if(e0<0 || (e0==0 && !TopLeft(a,b)) || e1<0 || (e1==0 && !TopLeft(b,c)) || e2<0 || (e2==0 && !TopLeft(c,a))) continue;
            float2 center=float2(x,y)+.5;
            precise float wb=Area(pc,pa,center)/denominator;
            precise float wc=Area(pa,pb,center)/denominator;
            precise float za=ca.z/ca.w;
            // Plane form preserves an exactly constant depth, including cross-raster ties.
            precise float z=za+(cb.z/cb.w-za)*wb+(cc.z/cc.w-za)*wc;
            uint address=geometryTable[record].reserved.y+(meshlet<<7u)+triangle;
            if(z>=0.0 && z<1.0) {uint64_t ignored;InterlockedMin(keys[y*width+x],MeteorVisibilityKey(z,address),ignored);}
        }
    }
}
struct ResolveVertex {float4 position:SV_Position;};
[shader("vertex")]
ResolveVertex ResolveVS(uint index:SV_VertexID)
{
    ResolveVertex v;v.position=float4(index==2u?3.0:-1.0,index==1u?3.0:-1.0,0,1);return v;
}
struct ResolvePixel {uint2 visibility:SV_Target0;float depth:SV_Depth;};
[shader("fragment")]
ResolvePixel ResolvePS(ResolveVertex input)
{
    uint2 pixel=uint2(input.position.xy);uint64_t key=completedKeys[pixel.y*width+pixel.x];
    ResolvePixel result;result.visibility=key==~uint64_t(0)?uint2(0):uint2(uint(key),uint(key>>32u));
    result.depth=key==~uint64_t(0)?1.0:asfloat(result.visibility.y);return result;
}
// Occluder stage: the same merged key, depth only (HZB source).
[shader("fragment")]
float ResolveDepthPS(ResolveVertex input) : SV_Depth
{
    uint2 pixel=uint2(input.position.xy);uint64_t key=completedKeys[pixel.y*width+pixel.x];
    return key==~uint64_t(0)?1.0:asfloat(uint(key>>32u));
}
