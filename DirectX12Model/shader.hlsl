struct PSInput
{
    float4 position : SV_POSITION;
	float4 normal : NORMAL;
	//float2 uv : TEXCOORD;
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
cbuffer MaterialBuffer : register(b2)
{
	float3 diffuse;
	float alpha;
	float3 ambient;
	float3 specular;
	float power;
	float3 emmisive;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);


PSInput VSMain(float4 position : POSITION, float4 normal : NORMAL, float2 uv : TEXCOORD)
{
    PSInput result;
	result.position = mul(world,position);
	result.position = mul(view,result.position);
	result.position = mul(proj,result.position);
	//result.color = color;
	//result.uv = uv;
	result.normal = mul(world,normalize(normal));

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float p = dot(input.normal, -light.xyz);
	p = p * 0.2 + 0.8;
	p = p * p;
	return p * float4(diffuse,1.0); // * g_texture.Sample(g_sampler, input.uv);
}