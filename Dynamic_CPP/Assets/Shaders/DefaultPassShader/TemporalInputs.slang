cbuffer TemporalCamera : register(b0)
{
    float4x4 inverseProjection, inverseView, currentProjection, previousProjection;
    uint width, height, reset, pad;
};
Texture2D<float> depth : register(t0);
#ifdef __spirv__
[[vk::image_format("rg16f")]]
#endif
RWTexture2D<float2> motion : register(u0);
#ifdef __spirv__
[[vk::image_format("r16f")]]
#endif
RWTexture2D<float> reactive : register(u1);
#ifdef __spirv__
[[vk::image_format("r16f")]]
#endif
RWTexture2D<float> transparency : register(u2);
#ifdef __spirv__
[[vk::image_format("r16f")]]
#endif
RWTexture2D<float> responsive : register(u3);
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if(id.x>=width||id.y>=height) return;
    const float z=depth.Load(int3(id.xy,0));
    const float2 ndc=(float2(id.xy)+0.5)/float2(width,height)*float2(2,-2)+float2(-1,1);
    float4 view=mul(float4(ndc,z,1),inverseProjection);
    // Sky reprojection is rotational only; translating the camera does not move
    // an infinitely distant background. Geometry uses the actual raster depth.
    if(z>=0.999999) view=float4(view.xyz,0);
    else view/=view.w;
    const float4 world=mul(view,inverseView);
    const float4 current=mul(world,currentProjection), previous=mul(world,previousProjection);
    const bool valid=reset==0 && current.w>0.000001 && previous.w>0.000001 && all(isfinite(previous));
    motion[id.xy]=valid?(previous.xy/previous.w-current.xy/current.w)*float2(0.5*width,-0.5*height):float2(0);
    reactive[id.xy]=0;transparency[id.xy]=0;responsive[id.xy]=valid?0:1;
}
