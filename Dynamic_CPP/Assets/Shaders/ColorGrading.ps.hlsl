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
    float time; // 0~1
}

Texture2D<float4> ColorTexture : register(t0);

Texture2D<float4> LUT : register(t1);
//Texture2D<float4> NewLUT : register(t2);


float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 baseColor = ColorTexture.Sample(LinearSampler, IN.texCoord);
    baseColor = saturate(baseColor);

    float red = baseColor.r * (Size - 1);
    float green = baseColor.g * (Size - 1);
    float blue = baseColor.b * (Size - 1);

    float redFrac = frac(red);
    float greenFrac = frac(green);
    float blueFrac = frac(blue);

    int red0 = (int) floor(red);
    int red1 = min(red0 + 1, Size - 1);

    int green0 = (int) floor(green);
    int green1 = min(green0 + 1, Size - 1);

    int blue0 = (int) floor(blue);
    int blue1 = min(blue0 + 1, Size - 1);

    int blueCol0 = blue0 % SizeRoot;
    int blueRow0 = blue0 / SizeRoot;

    int blueCol1 = blue1 % SizeRoot;
    int blueRow1 = blue1 / SizeRoot;

    int2 basePos00 = int2(blueCol0 * Size, blueRow0 * Size);
    int2 basePos01 = int2(blueCol1 * Size, blueRow1 * Size);

// Read 8 LUT values for trilinear interpolation
    float4 c000 = LUT.Load(int3(basePos00 + int2(red0, green0), 0));
    float4 c100 = LUT.Load(int3(basePos00 + int2(red1, green0), 0));
    float4 c010 = LUT.Load(int3(basePos00 + int2(red0, green1), 0));
    float4 c110 = LUT.Load(int3(basePos00 + int2(red1, green1), 0));

    float4 c001 = LUT.Load(int3(basePos01 + int2(red0, green0), 0));
    float4 c101 = LUT.Load(int3(basePos01 + int2(red1, green0), 0));
    float4 c011 = LUT.Load(int3(basePos01 + int2(red0, green1), 0));
    float4 c111 = LUT.Load(int3(basePos01 + int2(red1, green1), 0));

// Trilinear interpolation
    float4 c00 = lerp(c000, c100, redFrac);
    float4 c01 = lerp(c010, c110, redFrac);
    float4 c0 = lerp(c00, c01, greenFrac);

    float4 c10 = lerp(c001, c101, redFrac);
    float4 c11 = lerp(c011, c111, redFrac);
    float4 c1 = lerp(c10, c11, greenFrac);

    float4 lutColor = lerp(c0, c1, blueFrac);

// Blend LUT result with base color
    //float4 finalColor = lerp(baseColor, lutColor, lerpValue);
    float4 finalColor = lerp(baseColor, lutColor, min(time, 1.0f));
    finalColor = lerp(baseColor, finalColor, lerpValue);
    finalColor.a = 1.0f;
    return finalColor;
    
    
    
 //   float4 baseTexture = ColorTexture.Load(int3(IN.position.xy, 0));
 //   baseTexture = saturate(baseTexture);
 //   float red = baseTexture.r * (Size - 1);
 //   float redinterpol = frac(red);
 //   float green = baseTexture.g * (Size - 1);
 //   float greeninterpol = frac(green);
 //   float blue = baseTexture.b * (Size - 1);
 //   float blueinterpol = frac(blue);

	////Blue base value
 //   float row = trunc(blue / SizeRoot);
 //   float col = trunc(blue % SizeRoot);

 //   float2 blueBaseTable = float2(trunc(col * Size), trunc(row * Size));

 //   float4 b0r1g0;
 //   float4 b0r0g1;
 //   float4 b0r1g1;
 //   float4 b1r0g0;
 //   float4 b1r1g0;
 //   float4 b1r0g1;
 //   float4 b1r1g1;

	///*
	//We need to read 8 values (like in a 3d LUT) and interpolate between them.
	//This cannot be done with default hardware filtering so I am doing it manually.
	//Note that we must not interpolate when on the borders of tables!
	//*/

	////Red 0 and 1, Green 0
 //   float4 b0r0g0 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green, 0));

	//[branch]
 //   if (red < Size - 1)
 //       b0r1g0 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green, 0));
 //   else
 //       b0r1g0 = b0r0g0;

	//// Green 1
	//[branch]
 //   if (green < Size - 1)
 //   {
	//	//Red 0 and 1
 //       b0r0g1 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green + 1, 0));

	//	[branch]
 //       if (red < Size - 1)
 //           b0r1g1 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green + 1, 0));
 //       else
 //           b0r1g1 = b0r0g1;
 //   }
 //   else
 //   {
 //       b0r0g1 = b0r0g0;
 //       b0r1g1 = b0r0g1;
 //   }

	//[branch]
 //   if (blue < Size - 1)
 //   {
 //       blue += 1;
 //       row = trunc(blue / SizeRoot);
 //       col = trunc(blue % SizeRoot);

 //       blueBaseTable = float2(trunc(col * Size), trunc(row * Size));

 //       b1r0g0 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green, 0));

	//	[branch]
 //       if (red < Size - 1)
 //           b1r1g0 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green, 0));
 //       else
 //           b1r1g0 = b0r0g0;

	//	// Green 1
	//	[branch]
 //       if (green < Size - 1)
 //       {
	//		//Red 0 and 1
 //           b1r0g1 = LUT.Load(int3(blueBaseTable.x + red, blueBaseTable.y + green + 1, 0));

	//		[branch]
 //           if (red < Size - 1)
 //               b1r1g1 = LUT.Load(int3(blueBaseTable.x + red + 1, blueBaseTable.y + green + 1, 0));
 //           else
 //               b1r1g1 = b0r0g1;
 //       }
 //       else
 //       {
 //           b1r0g1 = b0r0g0;
 //           b1r1g1 = b0r0g1;
 //       }
 //   }
 //   else
 //   {
 //       b1r0g0 = b0r0g0;
 //       b1r1g0 = b0r1g0;
 //       b1r0g1 = b0r0g0;
 //       b1r1g1 = b0r1g1;
 //   }

 //   float4 result = lerp(lerp(b0r0g0, b0r1g0, redinterpol), lerp(b0r0g1, b0r1g1, redinterpol), greeninterpol);
 //   float4 result2 = lerp(lerp(b1r0g0, b1r1g0, redinterpol), lerp(b1r0g1, b1r1g1, redinterpol), greeninterpol);

 //   result = lerp(result, result2, blueinterpol);

 //   //result = lerp(baseTexture, result, lerpValue);
 //   result = lerp(baseTexture, result, min(time, 1.0f));
 //   result.a = 1.0f;
    
 //   return result;
}