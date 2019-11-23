struct PSInput
{
    float4 position : SV_POSITION;
    //float4 color : COLOR;
	float2 uv : TEXCOORD;
};
cbuffer ConstantBuffer : register(b0)
{
	float4x4 world;
	float4x4 view;
	float4x4 proj;
};
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);


PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD)
{
    PSInput result;
	result.position = mul(world,position);
	result.position = mul(view,result.position);
	result.position = mul(proj,result.position);
	//result.color = color;
	result.uv = uv;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return g_texture.Sample(g_sampler, input.uv);
}