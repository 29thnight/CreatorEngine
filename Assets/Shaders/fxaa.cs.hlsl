// FXAA compute shader
// AKA Poor mans MSAA


// Set shader
// Set Input SRV
// Set Output UAV
// Map/Update FxaaParameters
// Context->Dispatch( ceil(width/N), ceil(height/N), 1);
// Done...

#define N 16

// Map buffer
cbuffer FxaaParameters			: register(b0)
{
	int2	TextureSize;
	float	Bias;
	float	BiasMin;
	float	SpanMax;	
	float 	_spacer1;
	float	_spacer2;
	float 	_spacer3;
};

// Input must be tonemaped from HDR to SDR
// Input must be DXGI_FORMAT_R8G8B8A8_UNORM
Texture2D<float3> 	Input 		: register(t0);	// SRV
RWTexture2D<float3> Output 		: register(u0);	// UAV

// ---------------------------------------------------------------------- //
// Below is implementation

// ignore
// groupshared float3	ColorCache[N+2][N+2]; // Extend samples above range by 1 for each side
// groupshared float3	LumaCache[N+2][N+2];


float3 FilterResult(float3 color)
{
	return pow(color, 1.0f/2.2f);
}
float GetLuma(float3 color)
{
	return dot(color, float3(0.299f, 0.587f, 0.114f));
}
float3 GetColor(int2 pos)
{
	return pow(Input[pos], 2.2f);
}


[numthreads(N, N, 1)]
void main(uint3 ThreadID : SV_GroupThreadID, uint3 GroupID : SV_GroupID, uint3 DispatchID : SV_DispatchThreadID)
{
	// Luma could be stored in group local memory, for faster computation

	float3 colorN = GetColor(DispatchID.xy + int2(0,-1));
	float3 colorW = GetColor(DispatchID.xy + int2(-1,0));
	float3 colorM = GetColor(DispatchID.xy);
	float3 colorE = GetColor(DispatchID.xy + int2(1,0));
	float3 colorS = GetColor(DispatchID.xy + int2(0,1));
	float3 colorNW = GetColor(DispatchID.xy + int2(-1,-1));
	float3 colorNE = GetColor(DispatchID.xy + int2(1,-1));
	float3 colorSW = GetColor(DispatchID.xy + int2(-1,1));
	float3 colorSE = GetColor(DispatchID.xy + int2(1,1));

	float lumaN = GetLuma(colorN);
	float lumaW = GetLuma(colorW);
	float lumaM = GetLuma(colorM);
	float lumaE = GetLuma(colorE);
	float lumaS = GetLuma(colorS);
	float lumaNW = GetLuma(colorNW);
	float lumaNE = GetLuma(colorNE);
	float lumaSW = GetLuma(colorSW);
	float lumaSE = GetLuma(colorSE);

	float minluma = min(lumaM,min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float maxluma = max(lumaM,max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

	float range = minluma - maxluma;

	float luma = (lumaNW + lumaNE + lumaN + lumaSW + lumaSE + lumaS + lumaM) * 0.16667f;

	// Determine FXAA direction from luminocity
	float2 blurdir;
	blurdir.x = -((lumaNW + lumaN + lumaNE) - (lumaSW + lumaS + lumaSE)); // Invert x direction becuse of coordinates
	blurdir.y = ((lumaNW + lumaW + lumaSW) - (lumaNE + lumaE + lumaSE));

	float biasAmount = max((lumaNW + lumaNE + lumaN + lumaSW + lumaSE + lumaS) * (Bias*0.16667f), BiasMin);

	float scale = 1.0f / (min(abs(blurdir.x), abs(blurdir.y)) + biasAmount);

    float2 spanMax = float2(SpanMax, SpanMax);
	blurdir = blurdir * scale;

	// Clamp blurdir
	blurdir = clamp(blurdir,-spanMax,spanMax);

	// Sample around
	float3 interp1 = GetColor(DispatchID.xy + ceil(blurdir * float(-0.167)));
	float3 interp2 = GetColor(DispatchID.xy + ceil(blurdir * float(0.167)));
	float3 interp3 = GetColor(DispatchID.xy + ceil(blurdir * float(-0.5)));
	float3 interp4 = GetColor(DispatchID.xy + ceil(blurdir * float(0.5)));

	float3 interpolant = (interp1 + interp2 + interp3 + interp4) * 0.20f;

	float newluma = GetLuma(interpolant);

	if (newluma < minluma || luma > maxluma)
	{
		// Return close values with bias to original
		Output[DispatchID.xy] = FilterResult(interp1 * 0.3f + interp2 * 0.3f + colorM * 0.4f);
	}
	else
	{
		// Return new values
		Output[DispatchID.xy] = FilterResult((interp1 + interp2 + interp3 + interp4 + colorM) * 0.2f);
	}
}