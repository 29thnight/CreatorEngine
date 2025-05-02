#include "Sampler.hlsli"

static const float LutSize = 64;
static const float Size = 16;
static const float SizeRoot = 4;

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer ColorGradingCBuffer : register(b0)
{
    float lerpValue;
}

Texture2D<float4> ColorTexture : register(t0);

Texture2D<float4> LUT : register(t1);
//Texture2D<float4> NewLUT : register(t2);


float4 main(PixelShaderInput IN) : SV_TARGET
{
    //float4 baseColor = ColorTexture.Sample(LinearSampler, IN.texCoord);
    //baseColor = saturate(baseColor);
    //// 0~1을 0~15으로 변환하고 lerp를 위해 소수점을 구함.
    //float red = baseColor.r * (Size - 1);
    //float redinterpol = frac(red); // 소수점
    //float green = baseColor.g * (Size - 1);
    //float greeninterpol = frac(green);
    //float blue = baseColor.b * (Size - 1);
    //float blueinterpol = frac(blue);
    
    //float col = trunc(blue % SizeRoot); // 0~3 정수값.
    //float row = trunc(blue / SizeRoot); // 0~3 
    //float nextCol = trunc((blue + 1) % SizeRoot);
    //float nextRow = trunc((blue + 1) / SizeRoot);
    
    //float2 blueBaseTable = float2(trunc(col * Size), trunc(row * Size));
    //float2 nextBlueBaseTable = float2(trunc(nextCol * Size), trunc(nextRow * Size));
    
    //float4 color = LUT.Sample(PointSampler, float2(blueBaseTable.x + red, blueBaseTable.y + green) / 64);
    //float4 nextColor = LUT.Sample(PointSampler, float2(nextBlueBaseTable.x + red + 1, nextBlueBaseTable.y + green + 1) / 64);
    ////float4 color = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green, 0));
    ////float4 nextColor = LUT.Load(int3(nextBlueBaseTable.x + red + 1, nextBlueBaseTable.y + green + 1, 0));
    
    //float4 finalColor = float4(lerp(color.r, nextColor.r, redinterpol),
    //                            lerp(color.g, nextColor.g, greeninterpol),
    //                            lerp(color.b, nextColor.b, blueinterpol),
    //                            1.0f);
    //finalColor = lerp(baseColor, finalColor, lerpValue);
    //finalColor.a = 1.0f;
    //return finalColor;
    
    
    
    float4 baseTexture = ColorTexture.Load(int3(IN.position.xy, 0));
    baseTexture = saturate(baseTexture);
    float red = baseTexture.r * (Size - 1);
    float redinterpol = frac(red);
    float green = baseTexture.g * (Size - 1);
    float greeninterpol = frac(green);
    float blue = baseTexture.b * (Size - 1);
    float blueinterpol = frac(blue);

	//Blue base value
    float row = trunc(blue / SizeRoot);
    float col = trunc(blue % SizeRoot);

    float2 blueBaseTable = float2(trunc(col * Size), trunc(row * Size));

    float4 b0r1g0;
    float4 b0r0g1;
    float4 b0r1g1;
    float4 b1r0g0;
    float4 b1r1g0;
    float4 b1r0g1;
    float4 b1r1g1;

	/*
	We need to read 8 values (like in a 3d LUT) and interpolate between them.
	This cannot be done with default hardware filtering so I am doing it manually.
	Note that we must not interpolate when on the borders of tables!
	*/

	//Red 0 and 1, Green 0
    float4 b0r0g0 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green, 0));

	[branch]
    if (red < Size - 1)
        b0r1g0 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green, 0));
    else
        b0r1g0 = b0r0g0;

	// Green 1
	[branch]
    if (green < Size - 1)
    {
		//Red 0 and 1
        b0r0g1 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green + 1, 0));

		[branch]
        if (red < Size - 1)
            b0r1g1 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green + 1, 0));
        else
            b0r1g1 = b0r0g1;
    }
    else
    {
        b0r0g1 = b0r0g0;
        b0r1g1 = b0r0g1;
    }

	[branch]
    if (blue < Size - 1)
    {
        blue += 1;
        row = trunc(blue / SizeRoot);
        col = trunc(blue % SizeRoot);

        blueBaseTable = float2(trunc(col * Size), trunc(row * Size));

        b1r0g0 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green, 0));

		[branch]
        if (red < Size - 1)
            b1r1g0 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green, 0));
        else
            b1r1g0 = b0r0g0;

		// Green 1
		[branch]
        if (green < Size - 1)
        {
			//Red 0 and 1
            b1r0g1 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green + 1, 0));

			[branch]
            if (red < Size - 1)
                b1r1g1 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green + 1, 0));
            else
                b1r1g1 = b0r0g1;
        }
        else
        {
            b1r0g1 = b0r0g0;
            b1r1g1 = b0r0g1;
        }
    }
    else
    {
        b1r0g0 = b0r0g0;
        b1r1g0 = b0r1g0;
        b1r0g1 = b0r0g0;
        b1r1g1 = b0r1g1;
    }

    float4 result = lerp(lerp(b0r0g0, b0r1g0, redinterpol), lerp(b0r0g1, b0r1g1, redinterpol), greeninterpol);
    float4 result2 = lerp(lerp(b1r0g0, b1r1g0, redinterpol), lerp(b1r0g1, b1r1g1, redinterpol), greeninterpol);

    result = lerp(result, result2, blueinterpol);

    return result;
}