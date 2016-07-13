#include "FreeTypeTextRS.hlsli"

cbuffer FontParams : register(b0)
{
	float4 Color;
}

Texture2D<float> FontTex : register(t0);
SamplerState LinearSampler : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD0;
};

float GetAlpha(float2 Tex)
{
	return saturate(FontTex.Sample(LinearSampler, Tex));
}

[RootSignature(Text_RootSig)]
float4 main( PS_INPUT Input ) : SV_Target
{
	const float2 ShadowOffset = float2(0.5f / 1024, 0.5f / 1024);
	float Alpha1 = GetAlpha(Input.Tex) * Color.a;
	float Alpha2 = GetAlpha(Input.Tex - ShadowOffset) * Color.a;
	return float4(Color.rgb * Alpha1, lerp(Alpha2, 1, Alpha1));
	//return float4(Color.rgb, 1) * GetAlpha(Input.Tex) * Color.a;
}