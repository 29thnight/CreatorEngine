// Minimal fixed kernel. General module graph lowering is a subsequent gate.
struct Particle {
    float3 position; float age;
    float3 velocity; float lifetime;
    float4 color;
    float size; uint alive; uint id; uint reserved;
};
RWStructuredBuffer<Particle> particles : register(u0);
cbuffer Step : register(b0) {
    uint capacity; uint requested; uint flags; uint reserved;
    float dt; float lifetime; float size; float drag;
    float3 initialVelocity; float reserved2;
    float3 acceleration; float reserved3;
};
[numthreads(64,1,1)]
void Initialize(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x; if (i>=capacity) return;
    Particle p;
    p.position=0; p.age=0; p.velocity=initialVelocity; p.lifetime=lifetime;
    p.color=1; p.size=size; p.alive=i<min(requested,capacity) ? 1 : 0; p.id=i; p.reserved=0;
    particles[i]=p;
}
[numthreads(64,1,1)]
void Update(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x; if (i>=capacity) return;
    Particle p=particles[i]; if (p.alive==0 || dt==0) return;
    p.age+=dt;
    if (p.age>=p.lifetime) { p.alive=0; particles[i]=p; return; }
    if ((flags&1)!=0) p.velocity+=acceleration*dt;
    if ((flags&2)!=0) p.velocity*=exp(-drag*dt);
    p.position+=p.velocity*dt;
    particles[i]=p;
}
