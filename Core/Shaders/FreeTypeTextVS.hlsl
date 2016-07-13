#include "FreeTypeTextRS.hlsli"

cbuffer FontParams : register( b0 )
{
	float2 Scale;			// ����Ʈ ������
	float2 Offset;			// ����Ʈ ������
	float InvTexSize;		// 1 / �ؽ��� ������
	float TextHeight;		// �ؽ�Ʈ �ִ� ����
}

struct VS_INPUT
{
	float2 ScreenPos : POSITION;
	uint4  Glyph : TEXCOORD;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD0;
};

[RootSignature(Text_RootSig)]
VS_OUTPUT main(VS_INPUT Input, uint VertID : SV_VertexID)
{
	const float2 Pos0 = Input.ScreenPos;
	const float2 Pos1 = Input.ScreenPos + float2(Input.Glyph.z, TextHeight);
	const uint2 Tex0 = Input.Glyph.xy;
	const uint2 Tex1 = Input.Glyph.xy + float2(Input.Glyph.z, TextHeight);

	float2 Factor = float2(VertID & 1, (VertID >> 1) & 1);

	VS_OUTPUT Output;
	Output.Pos = float4(lerp(Pos0, Pos1, Factor) * Scale + Offset, 0, 1);
	Output.Tex = lerp(Tex0, Tex1, Factor) * InvTexSize;

	return Output;
}