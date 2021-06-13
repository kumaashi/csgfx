uint frame : register(b0);
RWTexture2D<float4> tex0 : register(u0);
RWTexture2D<float4> tex1 : register(u1);

[numthreads(8, 8, 1)]
void CSMain(
	uint3 groupId : SV_GroupID,
	uint3 groupThreadId : SV_GroupThreadID,
	uint3 dispatchThreadId : SV_DispatchThreadID,
	uint groupIndex : SV_GroupIndex)
{
	tex0[dispatchThreadId.xy] = float4((groupThreadId.xyz + groupId.xyz) % 2, 1);
}

