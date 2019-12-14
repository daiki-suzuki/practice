struct PSInput
{
    float4 position : SV_POSITION;
    //float4 color : COLOR;
	float2 uv : TEXCOORD;
	float4 normal : NORMAL;
};
cbuffer ConstantBuffer : register(b0)
{
	float4x4 world;
	float4x4 view;
	float4x4 proj;
};
cbuffer LightBuffer : register(b1)
{
	float3 light;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);


PSInput VSMain(float4 position : POSITION, float4 uv : TEXCOORD, float4 normal : NORMAL)
{
    PSInput result;
	result.position = mul(world,position);
	result.position = mul(view,result.position);
	result.position = mul(proj,result.position);
	//result.color = color;
	result.uv = uv;
	result.normal = mul(world,normalize(normal));

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float p = dot(input.normal, -light.xyz);
	p = p * 0.5 + 0.5;
	p = p * p;
	return p * g_texture.Sample(g_sampler, input.uv);
}