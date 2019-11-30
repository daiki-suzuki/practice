struct GSInput
{
	float4 position : SV_POSITION;
	float scale : PSIZE;
};
struct PSInput
{
    float4 position : SV_POSITION;
    //float4 color : COLOR;
	float2 uv : TEXCOORD;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);


GSInput VSMain(float4 position : POSITION,float scale : PSIZE)
{
	GSInput result;
	result.position = position;
	result.scale = scale;
	//result.color = color;
	//result.uv = uv;

    return result;
}

cbuffer ConstantBuffer : register(b0)
{
	float4x4 world;
	float4x4 view;
	float4x4 proj;
};
[maxvertexcount(6)]
void GSMain(point GSInput In[1], inout TriangleStream<PSInput> stream)
{
	PSInput v[4];
	float4 offset[4];
	float2 uv[4];
	offset[0] = float4(-0.5, 0.5, 0, 0);
	offset[1] = float4(0.5, 0.5, 0, 0);
	offset[2] = float4(-0.5, -0.5, 0, 0);
	offset[3] = float4(0.5, -0.5, 0, 0);
	uv[0] = float2(0.0,0.0);
	uv[1] = float2(1.0,0.0);
	uv[2] = float2(0.0,1.0);
	uv[3] = float2(1.0,1.0);
	for(int i = 0;i < 4;i++)
	{
		float4 position = In[0].position + (offset[i] * In[0].scale);
		position = mul(world,position);
		position = mul(view,position);
		v[i].position = mul(proj,position);
		v[i].uv = uv[i];
		stream.Append(v[i]);
	}
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return g_texture.Sample(g_sampler, input.uv);
}