#include "Includes/MaterialGraphSurfacePoint.slang"
#include "Includes/LxMeshletGpuContract.slang"
#include "Includes/MeteorVisibilityContract.slang"
cbuffer RasterConstants : register(b0) { float4x4 projection; uint width, height, recordCount, meshletCount; };
StructuredBuffer<LXFrameGeometryRecord> geometryTable : register(t0);
StructuredBuffer<uint4> metadata : register(t1); // visible offset, active/SW/double-sided flags
StructuredBuffer<uint> visibleIds : register(t2);
StructuredBuffer<uint64_t> completedKeys : register(t3);
StructuredBuffer<uint> classifiedRoutes : register(t4);
StructuredBuffer<LXSurfacePoint> vertices[4096] : register(t16);
ByteAddressBuffer topology[4096] : register(t4112);
RWStructuredBuffer<uint> routes : register(u0);
RWStructuredBuffer<uint64_t> keys : register(u1);
uint GroupIndex(uint3 group) { return group.y * 65535u + group.x; }
// Vertex streams are object space; the frame record carries object-to-world.
float4 Clip(uint record, uint vertex) {
    const float3 world=MeteorRecordWorldPosition(geometryTable[record],vertices[record-1u][vertex].position.xyz);
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
// One group owns one compacted input slot. Descriptor selection is group-uniform.
// The output is indexed by immutable frame meshlet address, never compacted order.
[numthreads(64,1,1)]
void Classify(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if(lane!=0u) return;
    uint index=GroupIndex(group);
    if(index>=meshletCount) return;
    uint record=MeteorVisibilityRecord(geometryTable,recordCount,index*128u+1u);
    if(record==0u) return;
    uint4 meta=metadata[record];
    if((meta.y&1u)==0u) return;
    uint base=(geometryTable[record].reserved.y-1u)/128u;
    uint work=index-base;
    if(work>=visibleIds[meta.x]) return;
    uint meshlet=visibleIds[meta.x+1u+work];
    LXMeshletDescriptor d=LXLoadMeshlet(topology[record-1u],meshlet);
    bool software=(meta.y&2u)!=0u;
    float2 lo=float2(1e30),hi=float2(-1e30);
    for(uint v=0;v<d.vertexCount && software;++v) {
        float4 c=Clip(record,LXMeshletVertex(topology[record-1u],d,v));
        // Clipped, nonfinite and large work belongs to HW; this is the only classifier.
        software=all(isfinite(c)) && c.w>0 && c.z>=0 && c.z<c.w && all(abs(c.xy)<=c.w);
        if(software) {float2 p=Screen(c);lo=min(lo,p);hi=max(hi,p);}
    }
    software=software && all(ceil(hi)-floor(lo)<=8.0);
    routes[base+meshlet]=software?2u:1u;
}
int Edge(int2 a,int2 b,int2 p) { int2 d=b-a,q=p-a;return d.x*q.y-d.y*q.x; }
bool TopLeft(int2 a,int2 b) {int2 d=b-a;return d.y<0 || (d.y==0 && d.x>0);}
float Area(float2 a,float2 b,float2 p) {float2 d=b-a,q=p-a;return d.x*q.y-d.y*q.x;}
[numthreads(64,1,1)]
void Rasterize(uint3 group : SV_GroupID,uint lane : SV_GroupIndex)
{
    uint index=GroupIndex(group);
    if(index>=meshletCount) return;
    if(classifiedRoutes[index]!=2u) return;
    uint record=MeteorVisibilityRecord(geometryTable,recordCount,index*128u+1u);
    uint base=(geometryTable[record].reserved.y-1u)/128u;
    uint meshlet=index-base;
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
